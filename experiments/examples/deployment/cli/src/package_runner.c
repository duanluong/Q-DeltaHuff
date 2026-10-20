/*
 * Host port of the firmware benchmark (Core/Src/algo_benchmark.c on the
 * STM32, main/algo_benchmark.c on the ESP32-S3).
 *
 * The decode flow is unchanged: chunked CRC32 over every byte read, magic
 * code, 40-byte header, Huffman codebook regen, chunked payload decode
 * (qhufdel_payload_stream_*), apply deltas to the current model, compare
 * with the next model chunk by chunk, then the CRC32 integrity check.
 *
 * Differences with the firmware:
 *   - FatFs (FIL / f_read / f_lseek) is replaced by stdio, or by a memory
 *     buffer with --in-memory;
 *   - DWT / CCOUNT cycles are replaced by a monotonic timer (bench_timer);
 *   - models are runtime buffers, so their sizes are checked against the
 *     package header instead of being trusted;
 *   - logs are printed for the first iteration only unless verbose.
 */
#include "package_runner.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bench_timer.h"
#include "crc32.h"
#include "decoder.h"

/* ============================================================
 *  State (globals, as in the firmware)
 * ============================================================ */

static qhufdel_package_header_t header = {0};
static qhufdel_huffman_table_t  table  = {0};
static int16_t symbol_list[QHUFDEL_HUFFMAN_SYMBOLS_NUM_MAX_SIZE];

/* Magic code + package header */
static uint8_t
    s_prefix_buf[QHUFDEL_PACKAGE_MAGIC_CODE_SIZE + QHUFDEL_PACKAGE_HEADER_SIZE];

/* Codebook section (length histogram + symbol list, padded to 8-byte
 * frames), sized for the worst case of the format */
#define CODEBOOK_BUF_SIZE                                    \
    ((((QHUFDEL_HUFFMAN_MAX_CODE_LEN * 8u)                   \
       + (QHUFDEL_HUFFMAN_SYMBOLS_NUM_MAX_SIZE * 16u) + 63u) \
      / 64u)                                                 \
     * 8u)
static uint8_t s_codebook_buf[CODEBOOK_BUF_SIZE];

/* Package source: a stdio file, or the whole package in memory */
typedef struct
{
    FILE *          fp;
    const uint8_t * mem;
    uint32_t        size;
    uint32_t        pos; /* f_tell() */
} pkg_file_t;

typedef struct
{
    pkg_file_t * fp;
    uint32_t     remaining; /* payload bytes not yet read */
} payload_source_t;

/* Ticks spent reading the package during the current iteration;
 * subtracted from the measured time so only decoding is benchmarked. */
static uint64_t s_io_ticks = 0;

/* Ticks spent comparing the restored values with the next model during the
 * current iteration; a verification step only, subtracted like the I/O. */
static uint64_t s_cmp_ticks = 0;

/* Running CRC32 over every package byte read */
static uint32_t s_crc = 0;

/* Chunk counters for the current iteration */
/* Every package read of the current iteration (magic + header, codebook,
 * payload chunks, padding, stored CRC) and the bytes actually read */
static uint32_t s_in_reads   = 0;
static uint32_t s_in_bytes   = 0;
/* qhufdel_payload_stream_decode() calls of the current iteration */
static uint32_t s_out_chunks = 0;

/* Per-iteration outcome */
static run_status_t s_status   = RUN_OK;
static uint32_t     s_mismatch = 0;

/* Run parameters */
static size_t    s_in_chunk_size    = RUNNER_DEFAULT_IN_CHUNK_SIZE;
static size_t    s_out_chunk_values = RUNNER_DEFAULT_OUT_CHUNK_VALUES;
static bool      s_log              = true;
static uint8_t * s_restored         = NULL; /* optional copy of the result */
static size_t    s_restored_cap     = 0;

/* Only the first few mismatches are recorded: once the bitstream is out of
 * sync every following value mismatches. */
#define BENCH_MISMATCH_PRINT_MAX 16U

/*
 * Diagnostics of one decoder_test() run. Nothing is printed inside the
 * timed region (console output would be measured as decode time): the
 * decode path only records what happened here, and bench_diag_print()
 * prints it after the timer is stopped.
 *
 * Error messages are format strings taking up to two uint64_t arguments.
 */
typedef struct
{
    uint32_t index;
    int16_t  got;
    uint8_t  expected;
} bench_mismatch_t;

typedef struct
{
    const char * fmt; /* NULL if no error */
    uint64_t     arg0;
    uint64_t     arg1;
} bench_error_t;

typedef struct
{
    bool   header_decoded; /* `header` holds a valid package header */
    size_t target_len;     /* next model size, for the size warning */

    bench_error_t decode_err; /* first decode error */

    bool             decode_done; /* every value decoded and compared */
    uint32_t         decoded;
    uint32_t         mismatch_count;
    uint32_t         mismatch_logged;
    bench_mismatch_t mismatches[BENCH_MISMATCH_PRINT_MAX];

    bench_error_t crc_err;     /* integrity check error */
    bool          crc_checked; /* stored / computed CRC are valid */
    uint32_t      stored_crc;
    uint32_t      computed_crc;

    bool completed; /* decode and integrity check both ran */
    bool decode_ok;
    bool crc_ok;
} bench_diag_t;

static bench_diag_t s_diag;

static void
bench_diag_decode_error (const char * fmt, uint64_t arg0, uint64_t arg1)
{
    if (s_diag.decode_err.fmt == NULL)
    {
        s_diag.decode_err.fmt  = fmt;
        s_diag.decode_err.arg0 = arg0;
        s_diag.decode_err.arg1 = arg1;
    }
}

static void
bench_diag_crc_error (const char * fmt, uint64_t arg0)
{
    s_diag.crc_err.fmt  = fmt;
    s_diag.crc_err.arg0 = arg0;
    s_diag.crc_err.arg1 = 0;
}

static void
status_raise (run_status_t st)
{
    if (st > s_status)
    {
        s_status = st;
    }
}

/* ============================================================
 *  Package I/O (bench_file_read / bench_file_seek in the firmware)
 * ============================================================ */

static size_t
bench_file_read (pkg_file_t * fp, void * dst, size_t len)
{
    uint64_t t0 = bench_timer_now();
    size_t   br;

    if (fp->mem != NULL)
    {
        size_t avail = (size_t) (fp->size - fp->pos);
        br           = (len < avail) ? len : avail;
        memcpy(dst, fp->mem + fp->pos, br);
    }
    else
    {
        br = fread(dst, 1, len, fp->fp);
    }
    fp->pos += (uint32_t) br;
    s_in_reads++;
    s_in_bytes += (uint32_t) br;

    s_io_ticks += bench_timer_now() - t0;
    return br;
}

static bool
bench_file_read_exact (pkg_file_t * fp, void * dst, size_t len)
{
    return bench_file_read(fp, dst, len) == len;
}

static bool
bench_package_read (pkg_file_t * fp, void * dst, size_t len)
{
    size_t br = bench_file_read(fp, dst, len);
    s_crc     = crc32_update(s_crc, (const uint8_t *) dst, br);
    return br == len;
}

static bool
bench_file_seek (pkg_file_t * fp, uint32_t ofs)
{
    uint64_t t0 = bench_timer_now();
    bool     ok = true;

    if (fp->mem == NULL)
    {
        ok = (fseek(fp->fp, (long) ofs, SEEK_SET) == 0);
    }
    if (ok)
    {
        fp->pos = ofs;
    }

    s_io_ticks += bench_timer_now() - t0;
    return ok;
}

/* bit_reader_fill_fn: feeds the payload bitstream chunk by chunk */
static size_t
payload_fill (void * ctx, uint8_t * dst, size_t cap)
{
    payload_source_t * src = (payload_source_t *) ctx;
    size_t             to_read
        = (src->remaining < cap) ? (size_t) src->remaining : cap;

    if (to_read == 0u)
    {
        return 0;
    }
    size_t br = bench_file_read(src->fp, dst, to_read);
    s_crc     = crc32_update(s_crc, dst, br);
    src->remaining -= (uint32_t) br;
    return br;
}

/* ============================================================
 *  Integrity check / compare (unchanged logic)
 * ============================================================ */

/*
 * Finishes the running CRC32 started in decoder_test(): reads whatever the
 * decoder did not consume (payload padding) up to the last 4 bytes, then
 * compares against the stored CRC32.
 */
static bool
package_integrity_check (pkg_file_t * fp, uint32_t size, uint8_t * chunk,
                         size_t chunk_cap)
{
    if (size < 4)
    {
        bench_diag_crc_error("Buffer too small to contain CRC32\n", 0);
        return false;
    }

    uint32_t payload_len = size - 4;
    uint32_t done        = fp->pos;

    while (done < payload_len)
    {
        size_t to_read = ((payload_len - done) < chunk_cap)
                             ? (size_t) (payload_len - done)
                             : chunk_cap;
        if (!bench_package_read(fp, chunk, to_read))
        {
            bench_diag_crc_error("Failed to read package at offset %" PRIu64 "\n",
                                 done);
            return false;
        }
        done += (uint32_t) to_read;
    }
    uint32_t crc = s_crc;

    // Extract the stored CRC32 from the last 4 bytes (little-endian)
    uint8_t raw[4];
    if (!bench_file_read_exact(fp, raw, 4))
    {
        bench_diag_crc_error("Failed to read stored CRC32\n", 0);
        return false;
    }
    uint32_t stored_crc = (uint32_t) raw[0] | ((uint32_t) raw[1] << 8)
                          | ((uint32_t) raw[2] << 16)
                          | ((uint32_t) raw[3] << 24);

    uint32_t computed_crc = crc32_finish(crc);

    s_diag.crc_checked  = true;
    s_diag.stored_crc   = stored_crc;
    s_diag.computed_crc = computed_crc;

    return (computed_crc == stored_crc);
}

/* Compares one chunk of restored values starting at model index `base`,
 * returns the number of mismatches in that chunk. `printed_count` is the
 * number of mismatches already found in previous chunks. */
static uint32_t
restored_data_compare_to_reference (const qhufdel_restored_data_t * restored,
                                    size_t len, uint32_t base,
                                    const unsigned char * reference,
                                    uint32_t printed_count)
{
    uint32_t mismatch_count = 0;

    for (uint32_t i = 0; i < len; ++i)
    {
        if (restored[i].value != reference[base + i])
        {
            if (printed_count < BENCH_MISMATCH_PRINT_MAX)
            {
                bench_mismatch_t * m
                    = &s_diag.mismatches[s_diag.mismatch_logged++];
                m->index    = base + i;
                m->expected = reference[base + i];
                m->got      = restored[i].value;
            }
            printed_count++;
            mismatch_count++;
        }
    }
    return mismatch_count;
}

/* ============================================================
 *  Decode
 * ============================================================ */

static bool
package_decode (pkg_file_t * fp, uint32_t size, const model_t * current,
                const model_t * target, uint8_t * in_chunk,
                qhufdel_restored_data_t * out_chunk)
{
    // Magic code + package header
    if (!bench_package_read(fp, s_prefix_buf, sizeof(s_prefix_buf)))
    {
        bench_diag_decode_error("Failed to read package header\n", 0, 0);
        status_raise(RUN_PACKAGE_ERROR);
        return false;
    }

    uint32_t offsetof = 0;
    if (qhufdel_magic_code_verify(s_prefix_buf, sizeof(s_prefix_buf))
        != M_SUCCESS)
    {
        bench_diag_decode_error("Magic code verification failed\n", 0, 0);
        status_raise(RUN_PACKAGE_ERROR);
        return false;
    }
    else
    {
        offsetof += QHUFDEL_PACKAGE_MAGIC_CODE_SIZE;
    }

    if (qhufdel_package_header_decode(
            s_prefix_buf, sizeof(s_prefix_buf), offsetof, &header)
        != M_SUCCESS)
    {
        bench_diag_decode_error("Failed to decode package header\n", 0, 0);
        status_raise(RUN_PACKAGE_ERROR);
        return false;
    }
    else
    {
        offsetof += QHUFDEL_PACKAGE_HEADER_SIZE;
    }
    s_diag.header_decoded = true;

    /* Host only: the models are runtime inputs, make sure the package
     * matches them before indexing into them */
    if (header.model_size > target->len)
    {
        bench_diag_decode_error(
            "Package restores %" PRIu64 " bytes but the next model has "
            "only %" PRIu64 " bytes: wrong model pair?\n",
            header.model_size,
            target->len);
        status_raise(RUN_INPUT_ERROR);
        return false;
    }

    // Codebook section, directly after the header in the file
    uint32_t payload_bits
        = (header.num_symbols * header.symbol_bit_width)
          + (((header.max_code_len - header.min_code_len) + 1) * 8);
    uint32_t frames      = (payload_bits + 63) / 64;
    uint32_t total_bytes = frames * 8;

    if (total_bytes > sizeof(s_codebook_buf)
        || !bench_package_read(fp, s_codebook_buf, total_bytes))
    {
        bench_diag_decode_error(
            "Failed to read codebook (%" PRIu64 " bytes)\n", total_bytes, 0);
        status_raise(RUN_PACKAGE_ERROR);
        return false;
    }

    if (qhufdel_huffman_codebook_regen(s_codebook_buf,
                                       total_bytes,
                                       0,
                                       header.min_code_len,
                                       header.max_code_len,
                                       &table,
                                       symbol_list,
                                       sizeof(symbol_list)
                                           / sizeof(symbol_list[0]))
        != M_SUCCESS)
    {
        bench_diag_decode_error("Failed to build Huffman table\n", 0, 0);
        status_raise(RUN_PACKAGE_ERROR);
        return false;
    }
    offsetof += total_bytes;

    // Payload: the file position is now at `offsetof`; stop before the CRC
    uint32_t payload_bytes = (header.payload_size + 7u) / 8u;
    if (offsetof + 4u > size)
    {
        bench_diag_decode_error("Package truncated before payload\n", 0, 0);
        status_raise(RUN_PACKAGE_ERROR);
        return false;
    }
    if (payload_bytes > size - 4u - offsetof)
    {
        payload_bytes = size - 4u - offsetof;
    }

    payload_source_t         src = {fp, payload_bytes};
    qhufdel_payload_stream_t ps;
    if (qhufdel_payload_stream_init(&ps,
                                    in_chunk,
                                    s_in_chunk_size,
                                    payload_fill,
                                    &src,
                                    &table,
                                    symbol_list,
                                    header.special_symbol,
                                    header.model_size)
        != M_SUCCESS)
    {
        bench_diag_decode_error("Failed to init payload stream\n", 0, 0);
        status_raise(RUN_PACKAGE_ERROR);
        return false;
    }

    const unsigned char * cur_data       = current->data;
    uint32_t              done           = 0;
    uint32_t              mismatch_count = 0;
    while (done < header.model_size)
    {
        int32_t n = qhufdel_payload_stream_decode(
            &ps, out_chunk, (uint32_t) s_out_chunk_values);
        if (n <= 0)
        {
            bench_diag_decode_error(
                "Failed to decode payload at index %" PRIu64 "\n", done, 0);
            status_raise(RUN_PACKAGE_ERROR);
            s_mismatch = mismatch_count;
            return false;
        }
        s_out_chunks++;

        for (int32_t i = 0; i < n; ++i)
        {
            if (out_chunk[i].is_changed == true)
            {
                /* Host only: a delta needs a current value to apply to */
                if (done + (uint32_t) i >= current->len)
                {
                    bench_diag_decode_error(
                        "Delta at index %" PRIu64 " is past the end of "
                        "the current model (%" PRIu64
                        " bytes): wrong model pair?\n",
                        done + (uint32_t) i,
                        current->len);
                    status_raise(RUN_INPUT_ERROR);
                    s_mismatch = mismatch_count;
                    return false;
                }
                out_chunk[i].value = out_chunk[i].value + cur_data[done + i];
            }
        }

        /* Test only: timed separately and excluded from the decode time */
        uint64_t t0 = bench_timer_now();
        mismatch_count += restored_data_compare_to_reference(
            out_chunk, (size_t) n, done, target->data, mismatch_count);
        s_cmp_ticks += bench_timer_now() - t0;

        if (s_restored != NULL && done + (uint32_t) n <= s_restored_cap)
        {
            for (int32_t i = 0; i < n; ++i)
            {
                s_restored[done + i] = (uint8_t) out_chunk[i].value;
            }
        }
        done += (uint32_t) n;
    }
    s_diag.decode_done    = true;
    s_diag.mismatch_count = mismatch_count;
    s_diag.decoded        = done;

    s_mismatch = mismatch_count;
    if (mismatch_count != 0)
    {
        status_raise(RUN_MISMATCH);
    }
    return (mismatch_count == 0);
}

/*
 * Decodes the package in `fp` without ever holding the whole package or the
 * whole restored model in RAM: the file is read `in_chunk` by `in_chunk`
 * and the payload is decoded out_chunk_values values at a time into
 * `out_chunk`, which is applied and compared before the next chunk.
 *
 * The file is read once, in order, and every byte goes into the running
 * CRC32 checked afterwards by package_integrity_check().
 *
 * Returns true if the package decoded and matches `target` exactly.
 */
static bool
decoder_test (pkg_file_t * fp, uint32_t size, const model_t * current,
              const model_t * target, uint8_t * in_chunk,
              qhufdel_restored_data_t * out_chunk)
{
    memset(&header, 0, sizeof(header));
    memset(&table, 0, sizeof(table));
    memset(symbol_list, 0, sizeof(symbol_list));

    if (!bench_file_seek(fp, 0))
    {
        bench_diag_decode_error("fseek failed\n", 0, 0);
        status_raise(RUN_INPUT_ERROR);
        return false;
    }
    s_crc        = crc32_start();
    s_in_reads   = 0;
    s_in_bytes   = 0;
    s_out_chunks = 0;
    s_mismatch   = 0;

    bool decode_ok
        = package_decode(fp, size, current, target, in_chunk, out_chunk);
    bool crc_ok
        = package_integrity_check(fp, size, in_chunk, s_in_chunk_size);

    if (!crc_ok)
    {
        status_raise(RUN_CRC_ERROR);
    }
    s_diag.completed = true;
    s_diag.decode_ok = decode_ok;
    s_diag.crc_ok    = crc_ok;
    return decode_ok && crc_ok;
}

/*
 * Prints what the last decoder_test() recorded in s_diag, in the order the
 * messages used to be printed. Call it outside the timed region.
 */
static void
bench_diag_print (void)
{
    const bench_diag_t * d = &s_diag;

    if (d->header_decoded)
    {
        printf("Header: model_size=%" PRIu32 " payload_bits=%" PRIu32
               " symbols=%u code_len=%u..%u symbol_bits=%u special=%d\n",
               header.model_size,
               header.payload_size,
               (unsigned) header.num_symbols,
               (unsigned) header.min_code_len,
               (unsigned) header.max_code_len,
               (unsigned) header.symbol_bit_width,
               (int) header.special_symbol);
        if (header.model_size < d->target_len)
        {
            printf("Warning: package restores %" PRIu32
                   " bytes, next model has %zu bytes: comparing the first %" PRIu32
                   " only\n",
                   header.model_size,
                   d->target_len,
                   header.model_size);
        }
    }
    for (uint32_t i = 0; i < d->mismatch_logged; i++)
    {
        printf("Mismatch at index %" PRIu32 ": expected %d, got %d\n",
               d->mismatches[i].index,
               d->mismatches[i].expected,
               d->mismatches[i].got);
    }
    if (d->decode_err.fmt != NULL)
    {
        printf(d->decode_err.fmt, d->decode_err.arg0, d->decode_err.arg1);
    }
    if (d->decode_done)
    {
        printf("Total mismatches: %" PRIu32 " / %" PRIu32 "\n",
               d->mismatch_count,
               d->decoded);
    }

    if (d->crc_err.fmt != NULL)
    {
        printf(d->crc_err.fmt, d->crc_err.arg0, d->crc_err.arg1);
    }
    if (d->crc_checked)
    {
        printf("Stored:   0x%08" PRIX32 "\n", d->stored_crc);
        printf("Computed: 0x%08" PRIX32 "\n", d->computed_crc);
        printf(d->crc_ok ? "Integrity OK\n"
                         : "Integrity FAILED - package corrupted or CRC "
                           "mismatch\n");
    }

    if (d->completed && !d->decode_ok && !d->crc_ok)
    {
        printf("Decode failed on corrupted data: package damaged?\n");
    }
}

/* ============================================================
 *  Public API
 * ============================================================ */

const char *
runner_status_str (run_status_t status)
{
    switch (status)
    {
        case RUN_OK:
            return "OK";
        case RUN_MISMATCH:
            return "MISMATCH (restored model != next model)";
        case RUN_PACKAGE_ERROR:
            return "PACKAGE ERROR (magic/header/codebook/payload)";
        case RUN_CRC_ERROR:
            return "CRC ERROR (package corrupted)";
        case RUN_INPUT_ERROR:
            return "INPUT ERROR";
    }
    return "UNKNOWN";
}

void
runner_run (const char * package_path, const model_t * current,
            const model_t * next, const runner_config_t * cfg,
            runner_result_t * result)
{
    memset(result, 0, sizeof(*result));
    result->name             = "decoder_test";
    result->ticks_min        = UINT64_MAX;
    result->in_chunk_size    = cfg->in_chunk_size;
    result->out_chunk_values = cfg->out_chunk_values;
    result->status           = RUN_INPUT_ERROR;

    s_in_chunk_size    = cfg->in_chunk_size;
    s_out_chunk_values = cfg->out_chunk_values;

    // The package stays on disk and is read chunk by chunk
    pkg_file_t pkg = {0};
    pkg.fp         = fopen(package_path, "rb");
    if (pkg.fp == NULL)
    {
        printf("Failed to open file: %s\n", package_path);
        return;
    }
    long fsize = -1;
    if (fseek(pkg.fp, 0, SEEK_END) == 0)
    {
        fsize = ftell(pkg.fp);
    }
    if (fsize <= 0 || fseek(pkg.fp, 0, SEEK_SET) != 0)
    {
        printf("Failed to get size of %s\n", package_path);
        fclose(pkg.fp);
        return;
    }
    pkg.size                  = (uint32_t) fsize;
    result->bytes_transferred = pkg.size;

    uint8_t * pkg_mem = NULL;
    /* --out: filled by an extra untimed pass after the timed iterations,
     * copying the values out is not part of the decode time */
    uint8_t * restored_buf = NULL;
    if (cfg->in_memory)
    {
        pkg_mem = malloc(pkg.size);
        if (pkg_mem == NULL
            || fread(pkg_mem, 1, pkg.size, pkg.fp) != pkg.size)
        {
            printf("Failed to load %s into memory\n", package_path);
            free(pkg_mem);
            fclose(pkg.fp);
            return;
        }
        fclose(pkg.fp);
        pkg.fp  = NULL;
        pkg.mem = pkg_mem;
    }

    // Fixed-size chunk buffers instead of the whole package / model
    size_t    in_chunk_size  = cfg->in_chunk_size;
    size_t    out_chunk_size = cfg->out_chunk_values
                            * sizeof(qhufdel_restored_data_t);
    uint8_t * in_chunk       = malloc(in_chunk_size);
    qhufdel_restored_data_t * out_chunk = malloc(out_chunk_size);
    if (in_chunk == NULL || out_chunk == NULL)
    {
        printf("Failed to allocate chunk buffers (%zu + %zu bytes)\n",
               in_chunk_size,
               out_chunk_size);
        goto cleanup;
    }
    result->heap_used_bytes = (uint32_t) (in_chunk_size + out_chunk_size);

    if (cfg->keep_restored)
    {
        s_restored_cap = next->len;
        restored_buf   = malloc(s_restored_cap);
        if (restored_buf == NULL)
        {
            printf("Failed to allocate the restored model (%zu bytes)\n",
                   s_restored_cap);
            goto cleanup;
        }
    }

    printf("Decoding %s (%" PRIu32 " bytes%s), chunks: %zu bytes in / %zu "
           "values out\n",
           package_path,
           pkg.size,
           cfg->in_memory ? ", in memory" : "",
           in_chunk_size,
           cfg->out_chunk_values);

    // Start CRC32 initialization in advance (builds the table)
    crc32_start();

    result->status = RUN_OK;
    for (uint32_t i = 0; i < cfg->iterations; i++)
    {
        s_log = cfg->verbose || (i == 0u);
        if (i == 1u && !cfg->verbose)
        {
            printf("... iterations 2..%" PRIu32
                   " run silently (use --verbose to log them)\n",
                   cfg->iterations);
        }

        /* Nothing is printed while the timer runs: decoder_test()
         * records its messages in s_diag instead */
        memset(&s_diag, 0, sizeof(s_diag));
        s_diag.target_len = next->len;
        s_status          = RUN_OK;
        s_io_ticks        = 0;
        s_cmp_ticks       = 0;
        uint64_t t0       = bench_timer_now();
        bool     ok       = decoder_test(
            &pkg, pkg.size, current, next, in_chunk, out_chunk);
        /* Package reads and the comparison with the next model happen
         * inside decoder_test; exclude them */
        uint64_t elapsed
            = (bench_timer_now() - t0) - s_io_ticks - s_cmp_ticks;

        if (s_log)
        {
            bench_diag_print();
        }

        if (!ok && s_status == RUN_OK)
        {
            s_status = RUN_PACKAGE_ERROR;
        }
        if (s_status > result->status)
        {
            result->status = s_status;
        }

        result->iterations++;
        result->in_reads       = s_in_reads;
        result->in_bytes       = s_in_bytes;
        result->out_chunks     = s_out_chunks;
        result->out_values     = s_diag.decoded;
        result->mismatch_count = s_mismatch;
        result->model_size     = header.model_size;
        if (!ok)
        {
            result->error_count++;
        }

        /* Wrong inputs give the same result every time, stop early */
        if (s_status == RUN_INPUT_ERROR)
        {
            break;
        }

        /* Only an iteration that decoded the whole package with a valid
         * CRC did the full amount of work and is timed; a decode that
         * stopped early would drag the statistics down. A mismatch against
         * the next model alone still decoded everything and stays timed. */
        if (!(s_diag.decode_done && s_diag.crc_ok))
        {
            continue;
        }

        result->valid_iterations++;
        result->ticks_io_sum += s_io_ticks;
        result->ticks_cmp_sum += s_cmp_ticks;
        result->ticks_sum += elapsed;
        if (elapsed < result->ticks_min)
        {
            result->ticks_min = elapsed;
        }
        if (elapsed > result->ticks_max)
        {
            result->ticks_max = elapsed;
        }
    }

    uint32_t valid = result->valid_iterations;
    if (valid == 0u)
    {
        result->ticks_min = 0;
    }
    else
    {
        result->time_min_us = bench_timer_ticks_to_us(result->ticks_min);
        result->time_max_us = bench_timer_ticks_to_us(result->ticks_max);
        result->time_avg_us = bench_timer_ticks_to_us(result->ticks_sum)
                              / valid;
        result->time_io_avg_us = bench_timer_ticks_to_us(result->ticks_io_sum)
                                 / valid;
        result->time_cmp_avg_us
            = bench_timer_ticks_to_us(result->ticks_cmp_sum) / valid;
        if (result->time_avg_us > 0.0)
        {
            result->throughput_KBps = ((double) pkg.size / 1024.0)
                                      / (result->time_avg_us / 1e6);
        }
    }

    /* Untimed extra pass for --out: decode once more and keep every
     * restored value (the timed iterations never copy them) */
    if (restored_buf != NULL && result->status <= RUN_MISMATCH)
    {
        s_log      = false;
        s_restored = restored_buf;
        memset(&s_diag, 0, sizeof(s_diag));
        s_diag.target_len = next->len;
        (void) decoder_test(
            &pkg, pkg.size, current, next, in_chunk, out_chunk);
        s_restored = NULL;

        if (s_diag.decode_done && s_diag.crc_ok)
        {
            result->restored     = restored_buf;
            result->restored_len = header.model_size;
            restored_buf         = NULL; /* ownership moves to the result */
        }
    }

cleanup:
    free(restored_buf);
    s_restored     = NULL;
    s_restored_cap = 0;
    if (pkg.fp != NULL)
    {
        fclose(pkg.fp);
    }
    free(pkg_mem);
    free(in_chunk);
    free(out_chunk);
}

void
runner_print (const runner_result_t * result)
{
    if (result->iterations == 0u)
    {
        printf("\n--- Benchmark: %s ---\n"
               "Benchmark did not run: setup failed (see the messages "
               "above)\n"
               "Result       : %s\n",
               result->name,
               runner_status_str(result->status));
        return;
    }

    printf("\n--- Benchmark: %s ---\n"
           "Iterations   : %" PRIu32 " (%" PRIu32 " valid, timed)\n"
           "Time us (min/avg/max): %.2f / %.2f / %.2f\n"
           "I/O us (avg, excluded): %.2f\n"
           "Compare us (avg, excluded): %.2f\n"
           "Reads        : %" PRIu32 " reads, %" PRIu32
           " bytes (chunks of up to %zu bytes)\n"
           "Decode calls : %" PRIu32 " calls, %" PRIu32
           " values (up to %zu values each)\n"
           "Package size : %" PRIu32 " bytes\n"
           "Model size   : %" PRIu32 " bytes\n"
           "Throughput   : %.2f KB/s\n"
           "Heap requested: %" PRIu32 " bytes (chunk buffers, not measured)\n"
           "Mismatches   : %" PRIu32 "\n"
           "Errors       : %" PRIu32 "\n"
           "Result       : %s\n",
           result->name,
           result->iterations,
           result->valid_iterations,
           result->time_min_us,
           result->time_avg_us,
           result->time_max_us,
           result->time_io_avg_us,
           result->time_cmp_avg_us,
           result->in_reads,
           result->in_bytes,
           result->in_chunk_size,
           result->out_chunks,
           result->out_values,
           result->out_chunk_values,
           result->bytes_transferred,
           result->model_size,
           result->throughput_KBps,
           result->heap_used_bytes,
           result->mismatch_count,
           result->error_count,
           runner_status_str(result->status));

    uint32_t excluded = result->iterations - result->valid_iterations;
    if (excluded > 0u)
    {
        printf("WARNING: %" PRIu32 " iteration(s) did not decode completely "
               "and are excluded from the timing%s\n",
               excluded,
               (result->valid_iterations == 0u) ? ", no valid timing" : "");
    }
}

void
runner_result_free (runner_result_t * result)
{
    free(result->restored);
    result->restored     = NULL;
    result->restored_len = 0;
}

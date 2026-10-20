#ifndef PACKAGE_RUNNER_H
#define PACKAGE_RUNNER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "model_loader.h"

/* Defaults identical to the firmware (algo_benchmark.h) */
#define RUNNER_DEFAULT_IN_CHUNK_SIZE    512U /* bytes read per fread() */
#define RUNNER_DEFAULT_OUT_CHUNK_VALUES 256U /* values decoded per chunk */

/* Ordered by severity: the worst status seen across iterations wins */
typedef enum
{
    RUN_OK = 0,
    RUN_MISMATCH,      /* decoded fine, restored model != next model */
    RUN_PACKAGE_ERROR, /* magic / header / codebook / payload decode failed */
    RUN_CRC_ERROR,     /* package CRC32 does not match */
    RUN_INPUT_ERROR,   /* cannot open package, models do not fit header */
} run_status_t;

typedef struct
{
    uint32_t iterations;
    size_t   in_chunk_size;    /* BENCH_IN_CHUNK_SIZE */
    size_t   out_chunk_values; /* BENCH_OUT_CHUNK_VALUES */
    bool     in_memory;        /* load the whole .bin first (no file I/O) */
    bool     verbose;          /* log every iteration, not only the first */
    bool     keep_restored;    /* keep the restored model for --out */
} runner_config_t;

/* Same fields as benchmark_result_t of the firmware, in host units */
typedef struct
{
    const char * name;
    uint32_t     iterations; /* iterations actually run */
    /* Iterations that decoded the whole package with a valid CRC: the only
     * ones timed (a mismatch against the next model alone is still timed) */
    uint32_t     valid_iterations;

    /* --- Timing (timer ticks + derived microseconds), I/O excluded, over
     * the valid iterations only --- */
    uint64_t ticks_min;
    uint64_t ticks_max;
    uint64_t ticks_sum;
    double   time_min_us;
    double   time_max_us;
    double   time_avg_us;

    /* --- Package I/O, excluded from the timing above --- */
    uint64_t ticks_io_sum;
    double   time_io_avg_us;

    /* --- Comparison with the next model (test only), excluded too --- */
    uint64_t ticks_cmp_sum;
    double   time_cmp_avg_us;

    /* --- Chunking (per iteration) --- */
    uint32_t in_reads;   /* package reads, of any size (last iteration) */
    uint32_t in_bytes;   /* package bytes actually read (last iteration) */
    uint32_t out_chunks; /* decode calls (last iteration) */
    uint32_t out_values; /* values decoded (last iteration) */
    size_t   in_chunk_size;
    size_t   out_chunk_values;

    /* --- Throughput --- */
    uint32_t bytes_transferred; /* package size */
    double   throughput_KBps;   /* package bytes / avg decode time */

    /* --- Memory --- */
    uint32_t heap_used_bytes;

    /* --- Reliability --- */
    uint32_t     error_count;
    uint32_t     mismatch_count; /* of the last iteration */
    uint32_t     model_size;     /* from the package header */
    run_status_t status;

    /* --- Restored model (only with keep_restored) --- */
    uint8_t * restored;
    size_t    restored_len;
} runner_result_t;

/*
 * Runs the firmware's decoder_test() `cfg->iterations` times on the package
 * at `package_path`, applying it to `current` and comparing with `next`.
 * Always fills `result` (free it with runner_result_free()).
 */
void runner_run(const char * package_path, const model_t * current,
                const model_t * next, const runner_config_t * cfg,
                runner_result_t * result);

/* Same report as the firmware's algo_benchmark_print() */
void runner_print(const runner_result_t * result);

void runner_result_free(runner_result_t * result);

const char * runner_status_str(run_status_t status);

#endif /* PACKAGE_RUNNER_H */

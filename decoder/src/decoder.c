/**
 * @file decoder.c
 * @author Long Hoang Thanh (you@domain.com)
 * @brief Q-HUFDEL package decoder: implementation of decoder.c
 * @version 0.1
 * @date 2026-08-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "decoder.h"

#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "bit_manager.h"

const char QHUFDEL_PACKAGE_MAGIC_CODE[QHUFDEL_PACKAGE_MAGIC_CODE_SIZE]
    = {'Q', '-', 'H', 'U', 'F', 'D', 'E', 'L'};

/**
 * @brief Read a 16-bit little-endian unsigned value from a byte buffer.
 *
 * @param[in] buffer Input buffer.
 * @param[in] offsetof  Byte offsetof of the first (least significant) byte.
 *
 * @return The 16-bit unsigned value.
 */
static uint16_t
u16le_read (const uint8_t * buffer, uint32_t offsetof)
{
    return (uint16_t) ((uint16_t) buffer[offsetof]
                       | ((uint16_t) buffer[offsetof + 1u] << 8u));
}

/**
 * @brief Read a 16-bit little-endian signed value from a byte buffer.
 *
 * @param bytes Input buffer.
 * @param offset Byte offsetof of the first (least significant) byte.
 * @return int16_t The 16-bit signed value.
 */
static int16_t
int16le_read (const uint8_t * bytes, int offset)
{
    uint16_t raw
        = (uint16_t) bytes[offset] | ((uint16_t) bytes[offset + 1] << 8);
    return (int16_t) raw;
}

/**
 * @brief Read a 32-bit little-endian value from a byte buffer.
 *
 * @param[in] buffer Input buffer.
 * @param[in] offsetof  Byte offsetof of the first (least significant) byte.
 *
 * @return The 32-bit value.
 */
static uint32_t
u32le_read (const uint8_t * buffer, uint32_t offsetof)
{
    return (uint32_t) buffer[offsetof]
           | ((uint32_t) buffer[offsetof + 1u] << 8u)
           | ((uint32_t) buffer[offsetof + 2u] << 16u)
           | ((uint32_t) buffer[offsetof + 3u] << 24u);
}

/**
 * Verifies that the buffer starts with the expected 8-byte magic code.
 *
 * @param buffer      Pointer to the byte buffer to check
 * @param buffer_len  Length of the buffer (to avoid reading out of bounds)
 * @return true if magic code matches, false otherwise
 */
M_RET
qhufdel_magic_code_verify(const uint8_t * buffer, size_t buffer_len)
{
    if (buffer == NULL || buffer_len < sizeof(QHUFDEL_PACKAGE_MAGIC_CODE))
    {
        return M_ERROR;
    }

    return (memcmp(buffer,
                   QHUFDEL_PACKAGE_MAGIC_CODE,
                   sizeof(QHUFDEL_PACKAGE_MAGIC_CODE))
            == 0)
               ? M_SUCCESS
               : M_ERROR;
}

M_RET
qhufdel_package_header_decode(const uint8_t * buffer, uint32_t len,
                              uint32_t                   offsetof,
                              qhufdel_package_header_t * p_header)
{
    uint32_t i;

    if ((buffer == NULL) || (p_header == NULL))
    {
        return M_ERROR;
    }
    if ((offsetof + QHUFDEL_PACKAGE_HEADER_SIZE) > len)
    {
        return M_ERROR;
    }

    /* Frame 2 */
    p_header->version_type    = buffer[offsetof + 0u];
    p_header->update_type     = buffer[offsetof + 1u];
    p_header->version_current = u16le_read(buffer, offsetof + 2u);
    p_header->version_update  = u16le_read(buffer, offsetof + 4u);
    p_header->num_symbols     = u16le_read(buffer, offsetof + 6u);

    /* Frame 3 (bytes 14-15 are reserved/padding and are not decoded) */
    p_header->min_code_len     = buffer[offsetof + 8u];
    p_header->max_code_len     = buffer[offsetof + 9u];
    p_header->data_bit_width   = buffer[offsetof + 10u];
    p_header->symbol_bit_width = buffer[offsetof + 11u];
    p_header->special_symbol   = int16le_read(buffer, offsetof + 12u);

    /* Frames 4-5: MD5 checksum, 16 raw bytes */
    for (i = 0u; i < QHUFDEL_TARGET_CHECKSUM_SIZE; i++)
    {
        p_header->checksum[i] = buffer[offsetof + 16u + i];
    }

    /* Frame 6 */
    p_header->model_size   = u32le_read(buffer, offsetof + 32u);
    p_header->payload_size = u32le_read(buffer, offsetof + 36u);

    return M_SUCCESS;
}

M_RET
qhufdel_huffman_table_build(const uint8_t * buffer, uint32_t len,
                            uint32_t offsetof, uint8_t mncl, uint8_t mxcl,
                            qhufdel_huffman_table_t * p_table)
{
    if (buffer == NULL || p_table == NULL)
    {
        return M_ERROR;
    }

    if (mncl == 0 || mxcl < mncl || mxcl > QHUFDEL_HUFFMAN_MAX_CODE_LEN)
    {
        return M_ERROR;
    }

    if ((offsetof + (uint32_t) (mxcl - mncl) + 1) > len)
    {
        return M_ERROR;
    }

    memset(p_table, 0, sizeof(qhufdel_huffman_table_t));

    p_table->min_code_len = mncl;
    p_table->max_code_len = mxcl;

    for (uint32_t i = mncl; i <= mxcl; i++)
    {
        p_table->bit_len_cnt[i] = buffer[offsetof + (i - mncl)];
    }

    uint32_t kraft_sum = 0; /* accumulate in units of 2^max_code_len */
    for (int l = p_table->min_code_len; l <= p_table->max_code_len; l++)
    {
        kraft_sum += (uint32_t) p_table->bit_len_cnt[l]
                     << (p_table->max_code_len - l);
    }

    if (kraft_sum > (1u << p_table->max_code_len))
    {
        return M_ERROR; /* over-subscribed, illegal code lengths */
    }
    /* kraft_sum < full is allowed (incomplete code, e.g. single symbol) */

    int16_t  code     = 0;
    uint16_t idx      = 0;
    int      prev_len = 0;

    /* Initialize unused low lengths' sentinels */
    for (uint8_t l = 1; l < p_table->min_code_len; l++)
    {
        p_table->code_first[l] = -1;
    }

    for (uint8_t l = p_table->min_code_len; l <= p_table->max_code_len; l++)
    {
        /* shift accumulated code left for each length step we
         * skipped, then again for this length itself */
        code = (int16_t) ((code + p_table->bit_len_cnt[prev_len])
                          << (l - prev_len));
        /* prev_len starts at 0 so first iteration just does code <<= l,
         * matching code_first[min] = 0 for the shortest length */

        p_table->code_first[l]  = (p_table->bit_len_cnt[l] > 0) ? code : -1;
        p_table->index_first[l] = idx;

        idx += p_table->bit_len_cnt[l];
        prev_len = l;
    }

    p_table->total_symbols = idx;

    return M_SUCCESS;
}

static M_RET
raw_symbol_read (bit_reader_t * br, qhufdel_raw_symbol_t * sym)
{
    uint32_t bits;
    if (bit_reader_get_bits(br, 9, &bits) != 0)
    {
        return M_ERROR;
    }
    sym->sign      = (uint8_t) ((bits >> 8) & 1);
    sym->magnitude = (uint8_t) (bits & 0xFF);
    sym->value
        = sym->sign ? -(int16_t) sym->magnitude : (int16_t) sym->magnitude;
    return M_SUCCESS;
}

static M_RET
qhufdel_symbol_read (bit_reader_t * br, const qhufdel_huffman_table_t * tbl,
                     int16_t * symbol_table)
{
    uint16_t pos = 0;

    for (int len = tbl->min_code_len; len <= tbl->max_code_len; len++)
    {
        for (uint8_t i = 0; i < tbl->bit_len_cnt[len]; i++)
        {
            qhufdel_raw_symbol_t raw;
            if (raw_symbol_read(br, &raw) != M_SUCCESS)
            {
                return M_ERROR; /* buffer ran out before all symbols read */
            }
            symbol_table[pos++] = raw.value;
        }
    }
    /* pos should now equal tbl->total_symbols by construction */
    return M_SUCCESS;
}

M_RET
qhufdel_huffman_codebook_regen(const uint8_t * buffer, uint32_t len,
                               uint32_t offsetof, uint8_t mncl, uint8_t mxcl,
                               qhufdel_huffman_table_t * p_table,
                               int16_t * p_out, uint32_t out_cap)
{
    M_RET ret;

    ret = qhufdel_huffman_table_build(
        buffer, len, offsetof, mncl, mxcl, p_table);
    if (ret != M_SUCCESS)
    {
        return M_ERROR;
    }

    size_t symbol_byte_offset = (offsetof + (uint32_t) (mxcl - mncl) + 1u);

    size_t symbol_list_len    = (size_t) (p_table->total_symbols * 9u);
    if (symbol_list_len % 8u == 0u)
    {
        symbol_list_len /= 8u;
    }
    else
    {
        symbol_list_len = (symbol_list_len / 8u) + 1u;
    }

    bit_reader_t br;
    bit_reader_init(&br, (buffer + symbol_byte_offset), symbol_list_len);

    ret = qhufdel_symbol_read(&br, p_table, p_out);

    return ret;
}

/**
 * @brief Decode one canonical Huffman symbol whose alphabet is
 *        arbitrary signed values (as opposed to sequential indices).
 *
 * @param symbol_table  int16_t array populated by
 *                       qhufdel_read_symbol_list(): the actual signed
 *                       symbol values, laid out in the same
 *                       (length, index_first) order as tbl.
 * @param sym_out        Out: the decoded signed value.
 * @return 0 success, -1 EOF (not enough bits left), -2 invalid code.
 */
static int
symbol_find_and_compare (bit_reader_t * br, const qhufdel_huffman_table_t * tbl,
                         const int16_t * symbol_table, int16_t * sym_out)
{
    uint32_t window;
    int      avail;
    bit_reader_peek(br, tbl->max_code_len, &window, &avail);

    for (int len = tbl->min_code_len; len <= tbl->max_code_len; len++)
    {
        if (tbl->bit_len_cnt[len] == 0)
        {
            continue;
        }
        if (avail < len)
        {
            return -1; /* not enough bits left for even this length */
        }

        int16_t code = (int16_t) (window >> (tbl->max_code_len - len));

        if (code < (int16_t) (tbl->code_first[len] + tbl->bit_len_cnt[len]))
        {
            int16_t rank = (int16_t) (code - tbl->code_first[len]);
            if (rank < 0)
            {
                continue;
            }
            bit_reader_consume(br, len);
            *sym_out = symbol_table[tbl->index_first[len] + rank];
            return 0;
        }
    }
    return -2; /* corrupt stream or mismatched table */
}

int32_t
qhufdel_payload_decode (const uint8_t * buf, size_t buf_len, uint32_t offsetof,
                        size_t                          bitstream_len_bits,
                        const qhufdel_huffman_table_t * tbl,
                        const int16_t * symbol_table, int16_t special_value,
                        qhufdel_restored_data_t * p_out_values,
                        uint32_t                  out_capacity)
{
    bit_reader_t br;
    bit_reader_init(&br, buf + offsetof, buf_len - offsetof);

    uint32_t cnt = 0;
    while (cnt <= out_capacity)
    {
        int16_t sym;
        int     rc = symbol_find_and_compare(&br, tbl, symbol_table, &sym);
        if (rc != 0)
        {
            printf("Error decoding symbol: %d\n", rc);
            return rc; /* -1 EOF, -2 corrupt code */
        }

        if (cnt > out_capacity)
        {
            printf("Output buffer overflow\n");
            return -4;
        }

        if (sym == special_value)
        {
            uint32_t extra;
            if (bit_reader_get_bits(&br, 8, &extra) != 0)
            {
                printf("Error reading escape byte\n");
                return -3; /* EOF reading escape byte */
            }

            p_out_values[cnt].value      = (int16_t) extra;
            p_out_values[cnt].is_changed = false;
            cnt++;
        }
        else
        {
            p_out_values[cnt].value      = sym;
            p_out_values[cnt].is_changed = true;
            cnt++;
        }
    }

    return 0;
}

M_RET
qhufdel_payload_stream_init (qhufdel_payload_stream_t * ps, uint8_t * in_chunk,
                             size_t in_chunk_cap, bit_reader_fill_fn fill,
                             void * fill_ctx, const qhufdel_huffman_table_t * tbl,
                             const int16_t * symbol_table,
                             int16_t special_value, uint32_t total_values)
{
    if (ps == NULL || in_chunk == NULL || in_chunk_cap == 0u || fill == NULL
        || tbl == NULL || symbol_table == NULL)
    {
        return M_ERROR;
    }

    bit_reader_init_stream(&ps->br, in_chunk, in_chunk_cap, fill, fill_ctx);
    ps->tbl           = tbl;
    ps->symbol_table  = symbol_table;
    ps->special_value = special_value;
    ps->remaining     = total_values;

    return M_SUCCESS;
}

int32_t
qhufdel_payload_stream_decode (qhufdel_payload_stream_t * ps,
                               qhufdel_restored_data_t * p_out,
                               uint32_t                  out_cap)
{
    if (ps == NULL || p_out == NULL || out_cap == 0u)
    {
        return -5;
    }

    uint32_t n   = (ps->remaining < out_cap) ? ps->remaining : out_cap;
    uint32_t cnt = 0;

    while (cnt < n)
    {
        int16_t sym;
        int     rc
            = symbol_find_and_compare(&ps->br, ps->tbl, ps->symbol_table, &sym);
        if (rc != 0)
        {
            printf("Error decoding symbol: %d\n", rc);
            return rc; /* -1 EOF, -2 corrupt code */
        }

        if (sym == ps->special_value)
        {
            uint32_t extra;
            if (bit_reader_get_bits(&ps->br, 8, &extra) != 0)
            {
                printf("Error reading escape byte\n");
                return -3; /* EOF reading escape byte */
            }

            p_out[cnt].value      = (int16_t) extra;
            p_out[cnt].is_changed = false;
        }
        else
        {
            p_out[cnt].value      = sym;
            p_out[cnt].is_changed = true;
        }
        cnt++;
    }

    ps->remaining -= cnt;
    return (int32_t) cnt;
}

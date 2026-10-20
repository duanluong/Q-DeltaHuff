/**
 * @file decoder.h
 * @author Long Hoang Thanh (you@domain.com)
 * @brief Q-HUFDEL package decoder: header for decoder.c
 * @version 0.1
 * @date 2026-07-31
 * @author Long Hoang Thanh
 * @see README.md for full format description and usage examples.
 *
 * @copyright Copyright (c) 2026
 *
 */
#ifndef DECODER_H
#define DECODER_H

#include <stdint.h>
#include <stddef.h>
#include "Types.h"
#include "bit_manager.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * Verifies that the buffer starts with the expected 8-byte magic code.
 *
 * @param buffer      Pointer to the byte buffer to check
 * @param buffer_len  Length of the buffer (to avoid reading out of bounds)
 * @return true if magic code matches, false otherwise
 */
M_RET
qhufdel_magic_code_verify(const uint8_t * buffer, size_t buffer_len);

/**
 * @brief Decode the package header (frames 2, 3, 4-5 and 6).
 *
 * @param[in]  buffer Input buffer containing the package.
 * @param[in]  len Buffer length in bytes.
 * @param[in]  offsetof Byte offsetof where the header (frame 2) starts.
 * @param[out] p_header Output header, filled on success.
 *
 * @note `SpecialSymbol` is decoded from the raw 2-byte field using the same
 * 9-bit sign-magnitude convention as the payload's literal-byte escape
 * value: bit 8 is the sign (1 = negative), bits 7-0 are the magnitude.
 *
 * @return `M_SUCCESS` on success, `M_ERROR` on NULL parameters or if the
 * header (`QHUFDEL_PACKAGE_HEADER_SIZE` bytes) does not fit within `BufferLen`.
 */
M_RET qhufdel_package_header_decode(const uint8_t * buffer, uint32_t len,
                                    uint32_t                   offsetof,
                                    qhufdel_package_header_t * p_header);

/**
 * @brief Build a canonical Huffman table
 *
 * @param[in] buffer Input buffer containing the packet.
 * @param[in] len Buffer length in bytes.
 * @param[in] offsetof Byte offsetof where section 1 starts.
 * @param[in] mncl Minimum code length in bits.
 * @param[in] mxcl Maximum code length in bits.
 * @param[out] p_table Output table, filled on success
 * @return M_RET `M_SUCCESS` on success, `M_ERROR` on invalid parameters or a
 * truncated buffer.
 */
M_RET qhufdel_huffman_table_build(const uint8_t * buffer, uint32_t len,
                                  uint32_t offsetof, uint8_t mncl, uint8_t mxcl,
                                  qhufdel_huffman_table_t * p_table);

/**
 * @brief Decode the Huffman symbol into signed value.
 *
 * @param[in] buffer Input buffer containing the packet.
 * @param[in] len Buffer length in bytes.
 * @param[in] offsetof Bit offsetof where the bitstream of symbol starts
 * @param[in] p_table Huffman table
 * @param[out] p_out Output array receiving the decoded signed values.
 * @param[in] out_cap Capacity of output array.
 * @return uint32_t Number of values decoded
 */
uint32_t qhufdel_huffman_symbol_decode(const uint8_t * buffer, uint32_t len,
                                       uint32_t                        offsetof,
                                       const qhufdel_huffman_table_t * p_table,
                                       int16_t * p_out, uint32_t out_cap);
/**
 * @brief Decode a full codebook in one call: builds the table from histogram,
 * then decode symbols following it.
 *
 * @param[in] buffer Input buffer
 * @param[in] len Buffer length in bytes
 * @param[in] offsetof Byte offsetof where the histogram starts
 * @param[in] mncl Minimum code length in bits.
 * @param[in] mxcl Maximum code length in bits.
 * @param[out] p_table Output table, filled on success
 * @param[out] p_out Output array receiving the decoded signed values.
 * @param[in] out_cap Capacity of output array.
 * @return M_RET M_RET `M_SUCCESS` on success, `M_ERROR` on invalid parameters
 * or a truncated buffer.
 */
M_RET qhufdel_huffman_codebook_regen(const uint8_t * buffer, uint32_t len,
                                     uint32_t offsetof, uint8_t mncl,
                                     uint8_t                   mxcl,
                                     qhufdel_huffman_table_t * p_table,
                                     int16_t * p_out, uint32_t out_cap);

/**
 * @brief Decode a payload bitstream using a previously-built codebook, with
 * escape-to-literal-byte support.
 *
 * The payload is a separate bitstream (its own buffer/frame, still padded to
 * a multiple of 8 bytes at the transport level) decoded with the canonical
 * Huffman table built by `Huffman_BuildTable()`. Symbols are decoded exactly
 * as in `Huffman_DecodeSection2()`, except: whenever a decoded symbol's value
 * equals `SpecialSymbolValue`, that symbol is NOT itself output. Instead, the
 * following 8 bits (MSB-first) are read as a literal byte and used as the
 * output value as-is (raw `[0, 255]`, no sign-magnitude interpretation). The
 * escape symbol is a normal entry of the codebook (own canonical code and
 * length) and counts as one decoded output, like any other symbol.
 *
 * @param[in]  buffer Input buffer containing the payload.
 * @param[in]  buf_len Buffer length in bytes (physical bounds
 * check).
 * @param[in] offsetof
 * @param[in]  bitstream_len_bits Exact length of the meaningful payload
 * bitstream, in bits (excludes any trailing transport-frame padding; must land
 * exactly on a symbol/literal boundary).
 * @param[in]  tbl Table previously built by `qhufdel_huffman_table_build()`.
 * @param[in]  symbol_table Array of signed symbol values, built by
 * `qhufdel_huffman_symbol_decode()` or `qhufdel_huffman_codebook_regen()`.
 * @param[in]  special_symbol Decoded value (in `[-255, 255]`) that triggers
 * literal-byte escape decoding.
 * @param[out] pOutValues Output array receiving the decoded values (normal
 * symbol values, or raw literal bytes for escaped positions).
 * @param[in]  out_capacity Capacity of `p_out_values[]`, in elements.
 *
 * @note `bitstream_len_bits` must end exactly on a code/literal-byte boundary;
 * if it cuts a code or a literal byte short, this is treated as corruption
 * and `M_ERROR` is returned. Any padding needed to fill the 8-byte transport
 * frame must be excluded from `bitstream_len_bits` by the caller.
 *
 * @return Number of values decoded (>= 0) on success, `M_ERROR` on failure
 * (invalid parameters, truncated buffer, corrupt/non-matching code, missing
 * literal byte after an escape symbol, or `p_out_values` too small).
 */
int32_t qhufdel_payload_decode(const uint8_t * buf, size_t buf_len,
                               uint32_t offsetof, size_t bitstream_len_bits,
                               const qhufdel_huffman_table_t * tbl,
                               const int16_t *                 symbol_table,
                               int16_t                         special_value,
                               qhufdel_restored_data_t *       p_out_values,
                               uint32_t                        out_capacity);

/**
 * @brief State of a chunked payload decode (see
 * `qhufdel_payload_stream_init()`).
 */
typedef struct
{
    bit_reader_t                    br;             /**< Streaming reader */
    const qhufdel_huffman_table_t * tbl;            /**< Codebook table */
    const int16_t *                 symbol_table;   /**< Codebook symbols */
    int16_t                         special_value;  /**< Escape symbol */
    uint32_t                        remaining;      /**< Values left */
} qhufdel_payload_stream_t;

/**
 * @brief Prepare a chunked payload decode. Input bytes are pulled through
 * @p fill into @p in_chunk, so only one input chunk and one output chunk
 * have to be in RAM at any time.
 *
 * @param[out] ps             Stream state to initialize.
 * @param[in]  in_chunk       Scratch buffer for input bytes.
 * @param[in]  in_chunk_cap   Size of @p in_chunk in bytes.
 * @param[in]  fill           Callback supplying the payload bytes in order,
 *                            starting at the first payload byte.
 * @param[in]  fill_ctx       Opaque context passed to @p fill.
 * @param[in]  tbl            Table built by `qhufdel_huffman_table_build()`.
 * @param[in]  symbol_table   Symbols from `qhufdel_huffman_codebook_regen()`.
 * @param[in]  special_value  Escape symbol (literal byte follows).
 * @param[in]  total_values   Number of values in the payload (model size).
 * @return `M_SUCCESS`, or `M_ERROR` on invalid parameters.
 */
M_RET qhufdel_payload_stream_init(qhufdel_payload_stream_t * ps,
                                  uint8_t * in_chunk, size_t in_chunk_cap,
                                  bit_reader_fill_fn fill, void * fill_ctx,
                                  const qhufdel_huffman_table_t * tbl,
                                  const int16_t * symbol_table,
                                  int16_t special_value, uint32_t total_values);

/**
 * @brief Decode the next chunk of at most @p out_cap values.
 *
 * Call repeatedly until it returns 0 (all `total_values` decoded).
 *
 * @return Number of values written to @p p_out (0 when finished), or a
 * negative error: -1 EOF, -2 corrupt code, -3 EOF reading escape byte,
 * -5 invalid parameters.
 */
int32_t qhufdel_payload_stream_decode(qhufdel_payload_stream_t * ps,
                                      qhufdel_restored_data_t *  p_out,
                                      uint32_t                   out_cap);
#ifdef __cplusplus
}
#endif
#endif

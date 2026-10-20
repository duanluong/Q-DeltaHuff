/**
 * @file bit_manager.h
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2026-08-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#ifndef BIT_MANAGER_H
#define BIT_MANAGER_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Chunk source for a streaming bit reader.
 *
 * Called when the current chunk is exhausted. Must copy up to @p cap bytes
 * into @p dst and return the number of bytes written; returning 0 means
 * end of stream (or read error).
 */
typedef size_t (*bit_reader_fill_fn)(void * ctx, uint8_t * dst, size_t cap);

/**
 * @brief Windowed bit reader (MSB-first)
 *
 */
typedef struct
{
    const uint8_t * buf;      /**< Source buffer */
    size_t          buf_len;  /**< Length of source buffer in bytes*/
    size_t          byte_pos; /**< Next unread byte index in buffer */
    uint64_t        acc;      /**< MSB-justified accumulator */
    int             acc_bits; /**< Valid bits currently in acc */

    /* Streaming mode only (fill == NULL for a plain in-memory buffer) */
    bit_reader_fill_fn fill;      /**< Chunk source callback */
    void *             fill_ctx;  /**< Opaque context passed to fill */
    uint8_t *          chunk;     /**< Scratch buffer refilled by fill */
    size_t             chunk_cap; /**< Capacity of chunk in bytes */
    size_t             base_pos;  /**< Bytes consumed by previous chunks */
} bit_reader_t;

/**
 * @brief Initialize a bit reader
 *
 * @param br
 * @param buf
 * @param len
 */
void bit_reader_init(bit_reader_t * br, const uint8_t * buf, size_t len);

/**
 * @brief Initialize a streaming bit reader that pulls its input chunk by
 * chunk through @p fill, so the whole bitstream never has to be in RAM.
 *
 * @param br
 * @param chunk     Scratch buffer, reused for every chunk
 * @param chunk_cap Size of @p chunk in bytes
 * @param fill      Chunk source callback
 * @param fill_ctx  Opaque context passed to @p fill
 */
void bit_reader_init_stream(bit_reader_t * br, uint8_t * chunk,
                            size_t chunk_cap, bit_reader_fill_fn fill,
                            void * fill_ctx);

/**
 * @brief
 *
 * @param br
 */
void bit_reader_refill(bit_reader_t * br);

/**
 * @brief Peek n bits without consuming. n may exceed acc_bits available
 *  near EOF; caller gets whatever is left, zero-padded, plus the
 *  true available count via *avail (may be NULL).
 *
 * @param br
 * @param n
 * @param out
 * @param avail
 */
void bit_reader_peek(bit_reader_t * br, int n, uint32_t * out, int * avail);

/**
 * @brief
 *
 * @param br
 * @param n
 */
void bit_reader_consume(bit_reader_t * br, int n);

/**
 * @brief
 *
 * @param br
 * @return size_t
 */
size_t bit_reader_bits_consumed(const bit_reader_t * br);

/**
 * @brief
 *
 * @param br
 * @param n
 * @param out
 * @return int
 */
int bit_reader_get_bits(bit_reader_t * br, int n, uint32_t * out);

/**
 * @brief
 *
 */
typedef struct
{
    uint8_t * buf;      /**< Destination buffer */
    size_t    buf_cap;  /**< Capacity of destination buffer */
    size_t    byte_pos; /**< Next write position in buffer */
    uint64_t  acc;      /**< MSB-justified pending bits */
    int       acc_bits; /**< Bits currently pending in acc */
} bit_writer_t;

/**
 * @brief
 *
 * @param bw
 * @param buf
 * @param cap
 */
void bit_writer_init(bit_writer_t * bw, uint8_t * buf, size_t cap);

/**
 * @brief
 *
 * @param bw
 * @param val
 * @param n
 * @return int
 */
int bit_writer_put_bits(bit_writer_t * bw, uint32_t val, int n);

/**
 * @brief
 *
 * @param bw
 * @return int
 */
int bit_writer_flush(bit_writer_t * bw);
#endif

#include "bit_manager.h"

void
bit_reader_init (bit_reader_t * br, const uint8_t * buf, size_t len)
{
    br->buf      = buf;
    br->buf_len  = len;
    br->byte_pos = 0;
    br->acc       = 0;
    br->acc_bits  = 0;
    br->fill      = NULL;
    br->fill_ctx  = NULL;
    br->chunk     = NULL;
    br->chunk_cap = 0;
    br->base_pos  = 0;
}

void
bit_reader_init_stream (bit_reader_t * br, uint8_t * chunk, size_t chunk_cap,
                        bit_reader_fill_fn fill, void * fill_ctx)
{
    bit_reader_init(br, chunk, 0);
    br->fill      = fill;
    br->fill_ctx  = fill_ctx;
    br->chunk     = chunk;
    br->chunk_cap = chunk_cap;
}

void
bit_reader_refill (bit_reader_t * br)
{
    while (br->acc_bits <= 56)
    {
        if (br->byte_pos >= br->buf_len)
        {
            if (br->fill == NULL)
            {
                break;
            }
            size_t n = br->fill(br->fill_ctx, br->chunk, br->chunk_cap);
            if (n == 0)
            {
                break; /* end of stream */
            }
            br->base_pos += br->buf_len;
            br->buf      = br->chunk;
            br->buf_len  = n;
            br->byte_pos = 0;
        }

        uint64_t byte = br->buf[br->byte_pos++];
        br->acc |= byte << (56 - br->acc_bits);
        br->acc_bits += 8;
    }
}

/** Peek n bits without consuming. n may exceed acc_bits available
 *  near EOF; caller gets whatever is left, zero-padded, plus the
 *  true available count via *avail (may be NULL). */
void
bit_reader_peek (bit_reader_t * br, int n, uint32_t * out, int * avail)
{
    if (br->acc_bits < n)
    {
        bit_reader_refill(br);
    }

    int have = br->acc_bits < n ? br->acc_bits : n;
    if (avail)
    {
        *avail = have;
    }

    *out = (uint32_t) (br->acc >> (64 - n)); /* zero-padded beyond acc_bits */
}

void
bit_reader_consume (bit_reader_t * br, int n)
{
    br->acc <<= n;
    br->acc_bits -= n;
}

size_t
bit_reader_bits_consumed (const bit_reader_t * br)
{
    return (br->base_pos + br->byte_pos) * 8 - (size_t) br->acc_bits;
}

int
bit_reader_get_bits (bit_reader_t * br, int n, uint32_t * out)
{
    int avail;
    bit_reader_peek(br, n, out, &avail);
    if (avail < n)
    {
        return -1;
    }
    bit_reader_consume(br, n);
    return 0;
}

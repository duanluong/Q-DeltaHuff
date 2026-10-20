/**
 ******************************************************************************
 * @file    crc32.c
 * @brief   Software CRC-32 (zlib/PKZIP/PNG/Ethernet compatible) implementation
 ******************************************************************************
 */

#include "crc32.h"

#define CRC32_POLYNOMIAL 0xEDB88320u
#define CRC32_INITIAL    0xFFFFFFFFu
#define CRC32_FINAL_XOR  0xFFFFFFFFu

static uint32_t crc32_table[256];
static uint8_t  crc32_table_ready = 0;

/**
 * @brief Build the lookup table once, on first use. On an F4 this takes
 * well under a millisecond and only runs a single time.
 *
 */
static void
crc32_build_table (void)
{
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
        {
            c = (c & 1) ? (CRC32_POLYNOMIAL ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_table_ready = 1;
}

uint32_t
crc32_start (void)
{
    if (!crc32_table_ready)
    {
        crc32_build_table();
    }
    return CRC32_INITIAL;
}

uint32_t
crc32_update (uint32_t crc, const uint8_t * data, size_t length)
{
    if (!crc32_table_ready)
    {
        crc32_build_table();
    }

    for (size_t i = 0; i < length; i++)
    {
        crc = crc32_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

uint32_t
crc32_finish (uint32_t crc)
{
    return crc ^ CRC32_FINAL_XOR;
}

uint32_t
crc32_calculate (const uint8_t * data, size_t length)
{
    uint32_t crc = crc32_start();
    crc          = crc32_update(crc, data, length);
    return crc32_finish(crc);
}

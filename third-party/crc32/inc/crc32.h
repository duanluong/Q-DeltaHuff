/**
 ******************************************************************************
 * @file    crc32.h
 * @brief   Software CRC-32 (zlib/PKZIP/PNG/Ethernet compatible) implementation
 *          Polynomial: 0xEDB88320 (reflected), Init: 0xFFFFFFFF,
 *          FinalXOR: 0xFFFFFFFF
 ******************************************************************************
 */

#ifndef CRC32_H
#define CRC32_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/**
 * @brief  Calculate standard CRC-32 over a byte buffer (single-shot).
 * @param  data   Pointer to input buffer
 * @param  length Number of bytes in buffer
 * @retval 32-bit CRC value (matches zlib crc32(), PKZIP, PNG, Ethernet FCS)
 */
uint32_t crc32_calculate(const uint8_t * data, size_t length);

/**
 * @brief  Initialize/return the starting CRC state for streaming calculation.
 * @retval Initial CRC accumulator value to pass into crc32_update()
 */
uint32_t crc32_start(void);

/**
 * @brief  Feed a chunk of data into an in-progress CRC calculation.
 *         Use this when data arrives in pieces (e.g. UART/DMA chunks,
 *         flash sectors) instead of one contiguous buffer.
 * @param  crc    Current CRC accumulator (from crc32_start() or a
 *                previous crc32_update() call)
 * @param  data   Pointer to this chunk of input data
 * @param  length Number of bytes in this chunk
 * @retval Updated CRC accumulator (NOT yet final -- do not compare
 *         this directly to a reference CRC until crc32_finish() is
 *         applied)
 */
uint32_t crc32_update(uint32_t crc, const uint8_t * data, size_t length);

/**
 * @brief  Finalize a streaming CRC calculation.
 * @param  crc Accumulator value after the last crc32_update() call
 * @retval Final CRC-32 value
 */
uint32_t crc32_finish(uint32_t crc);

#ifdef __cplusplus
}
#endif

#endif /* CRC32_H */

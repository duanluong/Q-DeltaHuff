/**
 * @file Types.h
 * @author Long Hoang Thanh (you@domain.com)
 * @brief Q-HUFDEL package decoder: common types for decoder.c
 * @version 0.1
 * @date 2026-08-02
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>

#define QHUFDEL_PACKAGE_MAGIC_CODE_SIZE                                 \
    8 /**< Size, in bytes, of the magic code used to identify a QHUFDEL \
         package */

#define QHUFDEL_HUFFMAN_MAX_CODE_LEN                 \
    32 /**< Maximum length of a Huffman code in bits \
        */

#define QHUFDEL_PACKAGE_HEADER_SIZE                  \
    (40u) /**< Size, in bytes, of the package header \
           */

#define QHUFDEL_TARGET_CHECKSUM_SIZE                                     \
    (16u) /**< Size, in bytes, of the MD5 checksum of the targetF stored \
           * across frames 4-5.                                          \
           */

#define QHUFDEL_PACKAGE_CHECKSUM_SIZE (4u) /**< CRC-32 field size in bytes */

#define QHUFDEL_HUFFMAN_SYMBOLS_NUM_MAX_SIZE \
    (512u) /**< Maximum number of symbols in a Huffman codebook */

typedef enum
{
    M_SUCCESS = 0,
    M_ERROR,
} M_RET;

/**
 * @brief Package header, spanning frames 2, 3, 4-5 and 6 (5 * 8-byte frames),
 * immediately preceding section 1 (the code length histogram).
 *
 * @par Frame 2 (8 bytes)
 * `VersionType`(1) + `UpdateType`(1) + `VersionCurrent`(2 LE) +
 * `VersionUpdate`(2 LE) + `NumHuffmanSymbols`(2 LE).
 *
 * @par Frame 3 (8 bytes)
 * `MinCodeLen`(1) + `MaxCodeLen`(1) + `ReplacedBits`(1) + `SymbolBits`(1) +
 * `SpecialSymbol`(2, raw 9-bit sign-magnitude) + 2 reserved/padding bytes.
 *
 * @par Frames 4-5 (16 bytes)
 * `Checksum`: MD5 digest, stored as raw bytes (equivalently, two 8-byte
 * little-endian quads, byte order preserved either way).
 *
 * @par Frame 6 (8 bytes)
 * `TargetSizeBytes`(4 LE) + `BitstreamSizeBits`(4 LE).
 */
typedef struct __attribute__((packed))
{
    uint8_t  version_type;    /**< Version Type of the package */
    uint8_t  update_type;     /**< Update Type of the package */
    uint16_t version_current; /**< Current version of the package */
    uint16_t version_update;  /**< Update version of the package */
    uint16_t num_symbols;     /**< Number of symbols in the package */
    uint8_t  min_code_len;    /**< Minimum code length in the Huffman coding */
    uint8_t  max_code_len;    /**< Maximum code length in the Huffman coding */
    uint8_t  data_bit_width;  /**< Width of the data for the Huffman coding */
    uint8_t symbol_bit_width; /**< Width of the symbol for the Huffman coding */
    int16_t special_symbol;   /**< Special symbol used in the Huffman coding */
    uint8_t checksum[QHUFDEL_TARGET_CHECKSUM_SIZE]; /**< MD5 checksum of the
                                                       package */
    uint32_t model_size;   /**< Size of the model in bytes */
    uint32_t payload_size; /**< Size of the bitstream payload in bits */
} qhufdel_package_header_t;

/**
 * @brief Canonical Huffman table, built once per section 1 histogram and
 *        reusable across multiple section 2 bitstreams sharing histogram.
 *
 * @details
 * This table implements the classic "canonical Huffman" decode scheme
 * (as used in DEFLATE/PNG-style codecs). Symbols are pre-sorted by
 * (code_len, symbol_value) into an external, flat @c symbol_table array
 * that is *not* part of this struct. For each code length L in
 * [min_code_len, max_code_len], all codes of that length form a
 * contiguous integer range starting at code_first[L], and the symbols
 * that own those codes form a contiguous run starting at
 * index_first[L] within the external symbol table.
 *
 * Decoding a code word `c` of length `L` therefore reduces to:
 * @code
 *   int16_t rank = c - code_first[L];              // 0-based rank within
 * length L uint16_t sym = symbol_table[index_first[L] + rank];
 * @endcode
 *
 * The table is built once from the length histogram in section 1 and
 * can be reused for decoding any number of section 2 bitstreams that
 * share the same canonical code assignment, avoiding per-stream
 * re-derivation of code_first/index_first/bit_len_cnt.
 */
typedef struct __attribute__((packed))
{
    /**
     * @brief First canonical code value assigned to each code length.
     *
     * @details
     * Indexed by code length L (1..QHUFDEL_HUFFMAN_MAX_CODE_LEN).
     * code_first[L] holds the smallest numeric Huffman code among all
     * codes of length L. Since canonical codes are assigned in
     * increasing, contiguous order per length, all valid codes of
     * length L lie in the half-open range:
     *
     *   [code_first[L], code_first[L] + bit_len_cnt[L])
     *
     * Standard canonical construction:
     *   code_first[L] = (code_first[L-1] + bit_len_cnt[L-1]) << 1
     *
     * signed (int16_t) rather than unsigned so that an explicit
     * sentinel value (typically -1) can mark lengths with
     * bit_len_cnt[L] == 0, i.e. "no codes of this length exist".
     * Decoders must check this sentinel (or bit_len_cnt[L]) before
     * comparing an incoming bit pattern against code_first[L], to
     * avoid false-matching an unused length.
     *
     * Index 0 is unused/reserved since a code length of 0 bits is not
     * meaningful; only indices 1..QHUFDEL_HUFFMAN_MAX_CODE_LEN are valid.
     */
    int16_t code_first[QHUFDEL_HUFFMAN_MAX_CODE_LEN + 1];

    /**
     * @brief Symbol-table index of the first symbol assigned to each
     *        code length.
     *
     * @details
     * Indexed by code length L. index_first[L] is the offset into the
     * external, canonically-sorted symbol_table array where the run of
     * symbols having code length L begins. Symbols within that run are
     * ordered ascending by symbol value, matching the ascending order
     * of their assigned codes (code_first[L], code_first[L]+1, ...).
     *
     * Built as a running cumulative sum over bit_len_cnt:
     *   index_first[L] = index_first[L-1] + bit_len_cnt[L-1]
     *
     * Used together with code_first[L] to map a decoded code word
     * directly to a symbol_table offset without a search:
     *   symbol_table[index_first[L] + (code - code_first[L])]
     *
     * uint16_t because it indexes into total_symbols (up to 65535
     * entries), which may exceed the range of a code length index.
     */
    uint16_t index_first[QHUFDEL_HUFFMAN_MAX_CODE_LEN + 1];

    /**
     * @brief Number of symbols encoded with each code length.
     *
     * @details
     * Indexed by code length L. bit_len_cnt[L] is the histogram count
     * of how many distinct symbols were assigned an L-bit canonical
     * code. This is the raw input derived from the section 1 length
     * histogram, and is the basis from which code_first[] and
     * index_first[] are both derived via cumulative sums.
     *
     * bit_len_cnt[0] should always be 0 (no symbol may have a
     * zero-length code). The sum of all entries must equal
     * total_symbols. This array is also used to validate that the
     * assigned lengths satisfy the Kraft inequality (i.e. that the
     * code is complete/well-formed) before the table is trusted for
     * decoding.
     *
     * uint8_t is sufficient since counts per individual length are
     * expected to stay within 0..255 for this format's symbol
     * alphabet size; widen if a single length could legitimately hold
     * more than 255 symbols.
     */
    uint8_t bit_len_cnt[QHUFDEL_HUFFMAN_MAX_CODE_LEN + 1];

    /**
     * @brief Shortest code length actually used by any symbol.
     *
     * @details
     * The smallest L for which bit_len_cnt[L] > 0. Used to bound the
     * decode loop's starting point (no need to test lengths shorter
     * than this, since no code of that length exists) and to validate
     * that at least one symbol exists in the histogram.
     */
    uint8_t min_code_len;

    /**
     * @brief Longest code length actually used by any symbol.
     *
     * @details
     * The largest L for which bit_len_cnt[L] > 0. Bounds the decode
     * loop's upper limit and determines how many bits must be
     * available/peeked from the bitstream in the worst case before a
     * symbol can be resolved. Must not exceed
     * QHUFDEL_HUFFMAN_MAX_CODE_LEN.
     */
    uint8_t max_code_len;

    /**
     * @brief Total number of distinct symbols represented by this table.
     *
     * @details
     * Equal to the sum of bit_len_cnt[L] over all valid L. Used to
     * size/validate the external symbol_table array (which must have
     * at least total_symbols entries) and as a sanity check when
     * constructing the table from the section 1 histogram — the
     * decoder can reject a malformed histogram if the sum doesn't
     * match an expected symbol count.
     */
    uint16_t total_symbols;
} qhufdel_huffman_table_t;

/**
 * @brief Represents a decoded raw symbol extracted from the Huffman bitstream.
 *
 * @details
 * This structure stores the intermediate representation obtained
 * immediately after Huffman decoding. Depending on the compression
 * format, the sign and magnitude fields may be decoded separately
 * from the Huffman code before being combined into the final signed
 * integer value.
 *
 * The @p value field contains the reconstructed signed integer for
 * convenience, avoiding repeated sign/magnitude conversion during
 * subsequent processing.
 */
typedef struct __attribute__((packed))
{
    /**
     * @brief Sign of the decoded symbol.
     *
     * @details
     * Encodes the sign of the symbol. A value of 0 typically
     * represents a non-negative value, while a value of 1 represents
     * a negative value. The exact encoding depends on the bitstream
     * specification.
     */
    uint8_t sign;

    /**
     * @brief Absolute magnitude of the decoded symbol.
     *
     * @details
     * Stores the unsigned magnitude extracted from the bitstream,
     * before applying the sign. The valid range is determined by the
     * compression format.
     */
    uint8_t magnitude;

    /**
     * @brief Final signed symbol value.
     *
     * @details
     * The signed integer reconstructed from the decoded sign and
     * magnitude. This is the value normally consumed by the
     * decompression or restoration stage.
     */
    int16_t value;

} qhufdel_raw_symbol_t;

/**
 * @brief Represents a restored data sample after decoding.
 *
 * @details
 * This structure contains the final reconstructed sample together
 * with a flag indicating whether its value differs from the reference
 * or previously known value. It is typically produced after applying
 * prediction, differential decoding, or restoration algorithms to the
 * raw Huffman-decoded symbol.
 */
typedef struct
{
    /**
     * @brief Final restored sample value.
     *
     * @details
     * The reconstructed signed integer after all restoration steps
     * have been completed.
     */
    int16_t value;

    /**
     * @brief Indicates whether the sample has changed.
     *
     * @details
     * Set to @c true if the restored value differs from its reference
     * value (for example, the corresponding value in a previous
     * version or frame). Otherwise, the flag is @c false.
     */
    bool is_changed;

} qhufdel_restored_data_t;

extern const char QHUFDEL_PACKAGE_MAGIC_CODE[];

#endif

/**
 * @file decoder_test.c
 * @brief Unit test for decoder.c
 */
#include "decoder.h"
#include "Types.h"

#include "unity.h"

#include <stdio.h>
#include <string.h>

void
setUp (void)
{
}

void
tearDown (void)
{
}

/*==============================================================================
 * qhufdel_package_header_decode
 *============================================================================*/

static const uint8_t header_with_magic_code[]
    = {0x51, 0x2D, 0x48, 0x55, 0x46, 0x44, 0x45, 0x4C, 0x01, 0x00, 0x00, 0x00,
       0x01, 0x00, 0xBF, 0x00, 0x03, 0x0C, 0x08, 0x09, 0xDA, 0x00, 0x00, 0x00,
       0xCC, 0x7F, 0xB1, 0xCE, 0xCA, 0x39, 0xE8, 0x0C, 0xE7, 0x9B, 0x51, 0xAA,
       0xFB, 0x3C, 0x3C, 0x45, 0x78, 0x1A, 0x01, 0x00, 0xC7, 0xB3, 0x06, 0x00};

static const size_t data_len
    = sizeof(header_with_magic_code) / sizeof(header_with_magic_code[0]);
static qhufdel_package_header_t g_package_header;

void
test_qhufdel_magic_code_verify_valid (void)
{
    TEST_ASSERT_EQUAL_INT32(
        M_SUCCESS, qhufdel_magic_code_verify(header_with_magic_code, data_len));
}

void
test_qhufdel_package_header_decode_valid (void)
{
    int32_t  ret = 0;
    uint32_t i;

    ret = qhufdel_package_header_decode(header_with_magic_code,
                                        data_len,
                                        QHUFDEL_PACKAGE_MAGIC_CODE_SIZE,
                                        &g_package_header);

    TEST_ASSERT_EQUAL_INT32(M_SUCCESS, ret);

    TEST_ASSERT_EQUAL_UINT8(0x01u, g_package_header.version_type);
    TEST_ASSERT_EQUAL_UINT8(0x00u, g_package_header.update_type);
    TEST_ASSERT_EQUAL_UINT16(0x0000u, g_package_header.version_current);
    TEST_ASSERT_EQUAL_UINT16(0x0001u, g_package_header.version_update);
    TEST_ASSERT_EQUAL_UINT16(191u, g_package_header.num_symbols);

    TEST_ASSERT_EQUAL_UINT8(3u, g_package_header.min_code_len);
    TEST_ASSERT_EQUAL_UINT8(12u, g_package_header.max_code_len);
    TEST_ASSERT_EQUAL_UINT8(8u, g_package_header.data_bit_width);
    TEST_ASSERT_EQUAL_UINT8(9u, g_package_header.symbol_bit_width);
    TEST_ASSERT_EQUAL_INT16(218u, g_package_header.special_symbol);

    for (i = 0u; i < QHUFDEL_TARGET_CHECKSUM_SIZE; i++)
    {
        TEST_ASSERT_EQUAL_UINT8((uint8_t) header_with_magic_code[24u + i],
                                g_package_header.checksum[i]);
    }

    TEST_ASSERT_EQUAL_UINT32(72312u, g_package_header.model_size);
    TEST_ASSERT_EQUAL_UINT32(439239u, g_package_header.payload_size);
}

void
test_qhufdel_package_header_decode_null_params (void)
{
    qhufdel_package_header_t header;

    TEST_ASSERT_EQUAL_INT32(
        M_ERROR, qhufdel_package_header_decode(NULL, data_len, 0u, &header));

    TEST_ASSERT_EQUAL_INT32(M_ERROR,
                            qhufdel_package_header_decode(
                                header_with_magic_code, data_len, 0u, NULL));
}

void
test_qhufdel_package_header_decode_nonzero_offset (void)
{
    static uint8_t           Buffer[8u + QHUFDEL_PACKAGE_HEADER_SIZE];
    qhufdel_package_header_t header;
    uint32_t                 i;

    /* 8 bytes of unrelated leading data (e.g. frame 1), then the header */
    for (i = 0u; i < 8u; i++)
    {
        Buffer[i] = 0xEEu;
    }
    for (i = 0u; i < QHUFDEL_PACKAGE_HEADER_SIZE; i++)
    {
        Buffer[8u + i] = header_with_magic_code[8u + i];
    }

    TEST_ASSERT_EQUAL_INT32(
        M_SUCCESS,
        qhufdel_package_header_decode(Buffer, sizeof(Buffer), 8u, &header));
    TEST_ASSERT_EQUAL_UINT16(191, header.num_symbols);
    TEST_ASSERT_EQUAL_INT16(218u, header.special_symbol);
    TEST_ASSERT_EQUAL_UINT32(439239u, header.payload_size);
}

/*==============================================================================
 * qhufdel_huffman_table_build
 *============================================================================*/

static const uint8_t g_huffman_metadata[]
    = {0x01, 0x00, 0x0B, 0x10, 0x0C, 0x0C, 0x2D, 0x1F, 0x1D, 0x22, 0x00, 0x41,
       0x60, 0x90, 0x38, 0x14, 0x04, 0x02, 0x02, 0x01, 0x81, 0x00, 0xA0, 0x68,
       0x74, 0x32, 0x17, 0x0A, 0x84, 0xC2, 0x20, 0xF0, 0x60, 0x38, 0x20, 0x12,
       0x0A, 0x05, 0x83, 0x01, 0xAD, 0xA8, 0x9C, 0x4A, 0x23, 0x10, 0x87, 0xC3,
       0x41, 0xC0, 0xF0, 0x80, 0x44, 0x24, 0x13, 0x8C, 0xC6, 0x22, 0xF1, 0x68,
       0xAC, 0x50, 0x28, 0x15, 0x0B, 0x05, 0xC3, 0x01, 0x9F, 0xDF, 0xEB, 0xF3,
       0xF8, 0xFB, 0xFD, 0xBE, 0xBF, 0x4F, 0x9F, 0xCB, 0xE3, 0xF0, 0xF7, 0xFB,
       0xBD, 0xB2, 0x08, 0xFC, 0x7A, 0x3B, 0x1C, 0x8D, 0xC6, 0x83, 0x41, 0xB0,
       0xE0, 0x74, 0x3C, 0x1F, 0x10, 0x3B, 0x1D, 0xAE, 0xE7, 0x7B, 0xC1, 0xE2,
       0xF2, 0x79, 0xBD, 0x1E, 0xAF, 0x67, 0xBB, 0xE1, 0xF2, 0xFA, 0x7D, 0xFF,
       0xFF, 0xDF, 0xDF, 0xE7, 0xB3, 0xD7, 0xEA, 0xF4, 0xFA, 0x3C, 0xFE, 0x6F,
       0x2C, 0x92, 0x47, 0x22, 0x90, 0x88, 0x44, 0x42, 0x31, 0x20, 0x98, 0x4E,
       0xE4, 0x73, 0x39, 0xDD, 0x0E, 0x97, 0x53, 0xAD, 0xF8, 0xFD, 0xF2, 0x78,
       0xFC, 0x5E, 0x1F, 0x07, 0x72, 0x59, 0x2B, 0x94, 0xCA, 0x24, 0xF2, 0x69,
       0x28, 0x94, 0x50, 0x29, 0x15, 0x0A, 0xC5, 0x82, 0xD1, 0x7B, 0x79, 0xBE,
       0xE0, 0x71, 0x38, 0xDC, 0xAF, 0xE7, 0xFF, 0x7F, 0xBD, 0xDD, 0xED, 0xF6,
       0xBB, 0x3D, 0x8E, 0x8C, 0xCE, 0x5D, 0x2D, 0x95, 0x0B, 0x86, 0x03, 0x11,
       0x90, 0xCC, 0x68, 0x35, 0x1B, 0x8E, 0x07, 0x23, 0xA1, 0xE1, 0x29, 0xA6,
       0xD6, 0x6B, 0xB6, 0x1B, 0x2D, 0xB6, 0xE3, 0x75, 0xC2, 0x00, 0x00, 0x00,
       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
       0x00, 0x00, 0x00, 0x00, 0x00};

static const size_t  g_huffman_metadata_len = sizeof(g_huffman_metadata);

static const int16_t g_symbol_list[]
    = {0,    -5,   -4,   -3,   -2,   -1,   1,    2,    3,    4,    5,    6,
       -14,  -12,  -11,  -10,  -9,   -8,   -7,   -6,   7,    8,    9,    10,
       11,   12,   13,   218,  -19,  -18,  -17,  -16,  -15,  -13,  14,   15,
       16,   17,   18,   19,   -25,  -24,  -23,  -22,  -21,  -20,  20,   21,
       22,   23,   24,   25,   -251, -250, -249, -248, -247, -246, -245, -244,
       -243, -242, -241, -240, -239, -238, -237, -32,  -31,  -30,  -29,  -28,
       -27,  -26,  26,   27,   28,   29,   30,   31,   32,   236,  237,  238,
       239,  240,  241,  242,  243,  244,  245,  246,  247,  248,  249,  250,
       251,  -255, -254, -253, -252, -236, -235, -234, -233, -232, -231, -230,
       -229, -36,  -35,  -34,  -33,  33,   34,   35,   36,   38,   39,   228,
       230,  231,  232,  233,  234,  235,  252,  253,  -228, -227, -226, -225,
       -224, -220, -44,  -43,  -41,  -40,  -39,  -38,  -37,  37,   40,   41,
       42,   43,   44,   45,   47,   222,  223,  224,  226,  227,  229,  254,
       255,  -223, -222, -221, -219, -218, -217, -216, -209, -51,  -46,  -45,
       -42,  46,   48,   49,   50,   51,   52,   53,   55,   56,   57,   58,
       60,   74,   211,  214,  215,  216,  217,  219,  220,  221,  225};

static const size_t g_symbol_count
    = sizeof(g_symbol_list) / sizeof(g_symbol_list[0]);

static qhufdel_huffman_table_t g_huffman_table_for_real_packet;

void
test_qhufdel_huffman_table_build_valid_real_data (void)
{
    int32_t ret = 0;

    ret         = qhufdel_huffman_table_build(g_huffman_metadata,
                                      g_huffman_metadata_len,
                                      0u,
                                      g_package_header.min_code_len,
                                      g_package_header.max_code_len,
                                      &g_huffman_table_for_real_packet);

    TEST_ASSERT_EQUAL_UINT8(M_SUCCESS, ret);
    TEST_ASSERT_EQUAL_UINT8(3u, g_huffman_table_for_real_packet.min_code_len);
    TEST_ASSERT_EQUAL_UINT8(12u, g_huffman_table_for_real_packet.max_code_len);
    TEST_ASSERT_EQUAL_UINT16(191u,
                             g_huffman_table_for_real_packet.total_symbols);

    TEST_ASSERT_EQUAL_UINT8(0u, g_huffman_table_for_real_packet.bit_len_cnt[0]);
    TEST_ASSERT_EQUAL_UINT8(0u, g_huffman_table_for_real_packet.bit_len_cnt[1]);
    TEST_ASSERT_EQUAL_UINT8(0u, g_huffman_table_for_real_packet.bit_len_cnt[2]);
    TEST_ASSERT_EQUAL_UINT8(1u, g_huffman_table_for_real_packet.bit_len_cnt[3]);
    TEST_ASSERT_EQUAL_UINT8(0u, g_huffman_table_for_real_packet.bit_len_cnt[4]);
    TEST_ASSERT_EQUAL_UINT8(11u,
                            g_huffman_table_for_real_packet.bit_len_cnt[5]);
    TEST_ASSERT_EQUAL_UINT8(16u,
                            g_huffman_table_for_real_packet.bit_len_cnt[6]);
    TEST_ASSERT_EQUAL_UINT8(12u,
                            g_huffman_table_for_real_packet.bit_len_cnt[7]);
    TEST_ASSERT_EQUAL_UINT8(12u,
                            g_huffman_table_for_real_packet.bit_len_cnt[8]);
    TEST_ASSERT_EQUAL_UINT8(45u,
                            g_huffman_table_for_real_packet.bit_len_cnt[9]);
    TEST_ASSERT_EQUAL_UINT8(31u,
                            g_huffman_table_for_real_packet.bit_len_cnt[10]);
    TEST_ASSERT_EQUAL_UINT8(29u,
                            g_huffman_table_for_real_packet.bit_len_cnt[11]);
    TEST_ASSERT_EQUAL_UINT8(34u,
                            g_huffman_table_for_real_packet.bit_len_cnt[12]);
}

/*==============================================================================
 * qhufdel_huffman_symbol_decode
 *============================================================================*/

/*==============================================================================
 * qhufdel_huffman_codebook_regen
 *============================================================================*/

void
test_qhufdel_huffman_codebook_regen_real_data (void)
{
    int16_t values[g_huffman_table_for_real_packet.total_symbols];
    M_RET   ret;

    ret = qhufdel_huffman_codebook_regen(
        g_huffman_metadata,
        g_huffman_metadata_len,
        0u,
        g_huffman_table_for_real_packet.min_code_len,
        g_huffman_table_for_real_packet.max_code_len,
        &g_huffman_table_for_real_packet,
        values,
        sizeof(values) / sizeof(values[0]));

    TEST_ASSERT_EQUAL_UINT8(M_SUCCESS, ret);
    TEST_ASSERT_EQUAL_UINT16(g_symbol_count,
                             g_huffman_table_for_real_packet.total_symbols);

    for (uint32_t i = 0u; i < g_symbol_count; i++)
    {
        TEST_ASSERT_EQUAL_INT16(g_symbol_list[i], values[i]);
    }
}

/*==============================================================================
 * qhufdel_payload_decode
 *============================================================================*/

void
test_qhufdel_payload_decode (void)
{
}

/*==============================================================================
 * qhufdel_package_checksum_compute / qhufdel_padding_bits_checksum_get /
 * qhufdel_offset_checksum_get / qhufdel_package_checksum_extract /
 * qhufdel_package_checksum_verify
 *============================================================================*/

void
test_qhufdel_package_checksum_compute_known_vector (void)
{
    /* Official CRC-32/ISO-HDLC (zlib) check value for ASCII "123456789" */
    static const uint8_t Check[]
        = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

    TEST_ASSERT_EQUAL_UINT32(
        0xCBF43926u, qhufdel_package_checksum_compute(Check, sizeof(Check)));
}

void
test_qhufdel_package_checksum_compute_empty (void)
{
    TEST_ASSERT_EQUAL_UINT32(0x00000000u,
                             qhufdel_package_checksum_compute(NULL, 0u));
}

void
test_qhufdel_checksum_padding_bits_get_and_offset_checksum_get (void)
{
    TEST_ASSERT_EQUAL_UINT32(32u, qhufdel_padding_bits_checksum_get(0u));
    TEST_ASSERT_EQUAL_UINT32(0u, qhufdel_padding_bits_checksum_get(32u));
    TEST_ASSERT_EQUAL_UINT32(19u, qhufdel_padding_bits_checksum_get(13u));
    TEST_ASSERT_EQUAL_UINT32(60u, qhufdel_padding_bits_checksum_get(100u));

    TEST_ASSERT_EQUAL_UINT32(4u, qhufdel_offset_checksum_get(0u));
    TEST_ASSERT_EQUAL_UINT32(4u, qhufdel_offset_checksum_get(13u));
    TEST_ASSERT_EQUAL_UINT32(20u, qhufdel_offset_checksum_get(100u));

    /* Field always ends exactly on an 8-byte boundary */
    {
        uint32_t BitsBeforeCrc;
        for (BitsBeforeCrc = 0u; BitsBeforeCrc < 200u; BitsBeforeCrc++)
        {
            uint32_t Padding = qhufdel_padding_bits_checksum_get(BitsBeforeCrc);
            uint32_t TotalBits = BitsBeforeCrc + Padding + 32u;
            TEST_ASSERT_EQUAL_UINT32(0u, TotalBits % 64u);
        }
    }
}

void
test_qhufdel_package_checksum_extract_valid_and_bounds (void)
{
    static const uint8_t Buffer[]
        = {0xAAu, 0xAAu, 0xAAu, 0xAAu, 0x78u, 0x56u, 0x34u, 0x12u};
    uint32_t CrcValue;

    TEST_ASSERT_EQUAL_INT32(M_SUCCESS,
                            qhufdel_package_checksum_extract(
                                Buffer, sizeof(Buffer), 4u, &CrcValue));
    TEST_ASSERT_EQUAL_UINT32(0x12345678u, CrcValue);

    /* Field does not fit: offset + 4 > BufferLen */
    TEST_ASSERT_EQUAL_INT32(M_ERROR,
                            qhufdel_package_checksum_extract(
                                Buffer, sizeof(Buffer), 5u, &CrcValue));

    TEST_ASSERT_EQUAL_INT32(
        M_ERROR,
        qhufdel_package_checksum_extract(NULL, sizeof(Buffer), 4u, &CrcValue));
    TEST_ASSERT_EQUAL_INT32(
        M_ERROR,
        qhufdel_package_checksum_extract(Buffer, sizeof(Buffer), 4u, NULL));
}

void
test_qhufdel_package_checksum_verify_match_and_mismatch (void)
{
    /* 9 data bytes ("123456789") + 3 zero padding bytes (72 meaningful
     * bits, 24 padding bits per qhufdel_padding_bits_checksum_get(72)) +
     * 4-byte CRC-32 (little-endian 0xCBF43926) = 16 bytes, a whole number
     * of 8-byte frames.
     */
    static const uint8_t Package[] = {
        '1',
        '2',
        '3',
        '4',
        '5',
        '6',
        '7',
        '8',
        '9', /* data (9 bytes) */
        0x00u,
        0x00u,
        0x00u, /* padding (3 bytes = 24 bits) */
        0x26u,
        0x39u,
        0xF4u,
        0xCBu /* CRC-32 LE of 0xCBF43926 */
    };
    uint32_t CrcByteOffset = qhufdel_offset_checksum_get(9u * 8u);

    TEST_ASSERT_EQUAL_UINT32(12u, CrcByteOffset);

    /* Correct coverage: only the 9 data bytes */
    TEST_ASSERT_EQUAL_INT32(M_SUCCESS,
                            qhufdel_package_checksum_verify(
                                Package, sizeof(Package), 9u, CrcByteOffset));

    /* Wrong coverage (includes the zero padding bytes) must not match */
    TEST_ASSERT_EQUAL_INT32(M_ERROR,
                            qhufdel_package_checksum_verify(
                                Package, sizeof(Package), 12u, CrcByteOffset));

    TEST_ASSERT_EQUAL_INT32(M_ERROR,
                            qhufdel_package_checksum_verify(
                                NULL, sizeof(Package), 9u, CrcByteOffset));

    /* CoverageByteLen larger than the buffer itself */
    TEST_ASSERT_EQUAL_INT32(
        M_ERROR,
        qhufdel_package_checksum_verify(
            Package, sizeof(Package), sizeof(Package) + 1u, CrcByteOffset));
}

int
main (void)
{
    UNITY_BEGIN();

    RUN_TEST(test_qhufdel_magic_code_verify_valid);
    RUN_TEST(test_qhufdel_package_header_decode_valid);
    RUN_TEST(test_qhufdel_package_header_decode_null_params);
    RUN_TEST(test_qhufdel_package_header_decode_nonzero_offset);

    RUN_TEST(test_qhufdel_huffman_table_build_valid_real_data);

    RUN_TEST(test_qhufdel_huffman_codebook_regen_real_data);

    RUN_TEST(test_qhufdel_package_checksum_compute_known_vector);
    RUN_TEST(test_qhufdel_package_checksum_compute_empty);
    RUN_TEST(test_qhufdel_checksum_padding_bits_get_and_offset_checksum_get);
    RUN_TEST(test_qhufdel_package_checksum_extract_valid_and_bounds);
    RUN_TEST(test_qhufdel_package_checksum_verify_match_and_mismatch);

    return UNITY_END();
}

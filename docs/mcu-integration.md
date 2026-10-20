# Q-HUFDEL decoder on an MCU

This guide explains how to integrate the Q-HUFDEL decoder with a microcontroller. A Q-HUFDEL package contains a Huffman-compressed delta between two versions of a quantized model. The decoder reads the package, applies the delta to the current model, and produces the next version.

## 1. Integration overview

The integration has three layers:

| Layer                          | Responsibility                                                                       | MCU-specific? |
| ------------------------------ | ------------------------------------------------------------------------------------ | ------------- |
| Decoder and checksum libraries | Parse the header, build the Huffman table, decode the payload, and calculate CRC-32  | No            |
| Glue code                      | Control the decode sequence, validate the header, apply deltas, and verify checksums | Usually no    |
| Port layer                     | Read package bytes and write the new model                                           | Yes           |

The port layer is the only part that normally needs to be rewritten for a new
MCU or storage device.

The data flow is one-way:

```text
package source -> port_pkg_read -> header/codebook -> payload decoder
                                                   -> delta + current model
                                                   -> port_model_write
```

At any point, RAM contains only the header, Huffman table, symbol list, input chunk, and output chunk.

Add these files to the build:

```text
decoder/src/decoder.c
decoder/src/bit_manager.c
crc32/src/crc32.c
md5/src/md5_hash.c       # omit when using a platform MD5
```

## 2. Port layer

Only these operations depend on the MCU:

```c
size_t port_pkg_read(uint8_t *dst, size_t len);
bool port_model_write(uint32_t base, const uint8_t *src, size_t len);
```

`port_pkg_read` must read sequentially from the start of the package and return the number of bytes actually written. It should wait until `len` bytes are available, returning fewer only at end-of-package or on an error.

Typical implementations are:

| Source                          | Implementation                                           |
| ------------------------------- | -------------------------------------------------------- |
| SD card                         | `f_read()`, preferably in 512-byte chunks                |
| Internal or memory-mapped flash | `memcpy()` from the mapped package                       |
| SPI/QSPI flash                  | Read commands or XIP                                     |
| ESP-IDF partition               | `esp_partition_read()` or a mapped partition             |
| UART/BLE/USB OTA                | Read from a ring buffer and wait for the requested bytes |

For a streaming transport, the sender must provide the package size first.

The payload callback has this prototype:

```c
typedef size_t (*bit_reader_fill_fn)(void *ctx, uint8_t *dst, size_t cap);
```

The glue code should normally provide this callback. It calls `port_pkg_read`, updates the CRC, and never reads beyond the payload boundary. Returning fewer than `cap` bytes is allowed; returning zero means end of data or an error. If DMA is used, wait for the transfer to finish before returning. The callback runs on the decoder's stack, so include the storage driver in the stack-depth calculation.

`port_model_write` receives output chunks with `base` increasing from zero to `model_size`. It must return `false` on a write error.

Use a separate RAM buffer or A/B flash slots whenever possible. Do not activate a new model until decoding and both checksum checks succeed.

## 3. Decoding sequence

The glue code must perform these steps:

1. Read and verify the 8-byte `Q-HUFDEL` magic.
2. Read and decode the 40-byte little-endian header.
3. Validate the header against the installed model: `model_size`, `version_current`, `min_code_len`, `max_code_len`, `num_symbols`, and `symbol_bit_width`.
4. Calculate and read the codebook size:

   ```c
   uint32_t hist = (uint32_t)(hdr.max_code_len - hdr.min_code_len) + 1u;
   uint32_t bits = (uint32_t)hdr.num_symbols * hdr.symbol_bit_width
                 + hist * 8u;
   uint32_t codebook_len = ((bits + 63u) / 64u) * 8u;
   ```

5. Call `qhufdel_huffman_table_build()` first, check `total_symbols`, then call `qhufdel_huffman_codebook_regen()`. The latter does not safely enforce the symbol-buffer capacity on its own.
6. Set `payload_end` to the current position plus `ceil(payload_size / 8)` bytes, never beyond the four CRC bytes.
7. Initialize `qhufdel_payload_stream_t` and repeatedly call
   `qhufdel_payload_stream_decode()` until `model_size` values are produced.
8. For each output value:

   ```c
   int16_t value = out[i].value;
   if (out[i].is_changed)       /* value is a delta */
       value = (int16_t)(value + current[base + i]);
   bytes[i] = (uint8_t)value;   /* otherwise value is a literal byte */
   ```

9. Read any remaining padding, then read and compare the little-endian CRC-32.
10. Finish MD5 and compare it with the header checksum.

`is_changed == true` means that `value` is a delta; it does not mean that the resulting byte changed. `false` means that `value` is already a literal byte.

The payload stream keeps pointers to the input buffer, table, and symbol list. Those objects must remain valid and unchanged until decoding finishes. A typical maximum codebook buffer is 608 bytes:

```c
#define CODEBOOK_MAX \
    ((((QHUFDEL_HUFFMAN_MAX_CODE_LEN * 8u) + \
       (QHUFDEL_HUFFMAN_SYMBOLS_NUM_MAX_SIZE * 9u) + 63u) / 64u) * 8u)
```

Suggested chunk sizes are 512 input bytes for SD cards and 32–64 bytes for mapped flash. Keep the table, symbol list, and input buffer alive until decoding finishes.

## 4. Checksums and safe activation

CRC-32 covers every package byte except the final four CRC bytes and is checked after decoding. MD5 covers the complete new model and confirms that the package was applied to the intended model. Reverse the bytes in each 8-byte half of the header digest before comparing:

```c
for (uint32_t i = 0; i < 8u; ++i) {
    expected[i]    = hdr.checksum[7u - i];
    expected[8+i]  = hdr.checksum[15u - i];
}
```

Use a staging buffer or A/B slots; a failed decode may have written partial output. The current encoder may hash the textual `.h` file instead of model bytes, which causes the device-side MD5 check to fail until the encoder hashes the exact target bytes.

## 5. API and return values

Main decoder functions:

| Function                         | Purpose                                      |
| -------------------------------- | -------------------------------------------- |
| `qhufdel_magic_code_verify`      | Verify the package magic                     |
| `qhufdel_package_header_decode`  | Decode the 40-byte header                    |
| `qhufdel_huffman_table_build`    | Build the canonical table from the histogram |
| `qhufdel_huffman_codebook_regen` | Read symbols and regenerate the table        |
| `qhufdel_payload_stream_init`    | Initialize streamed payload decoding         |
| `qhufdel_payload_stream_decode`  | Decode the next output chunk                 |
| `crc32_start/update/finish`      | Stream CRC-32                                |
| `md5_hash_start/update/finish`   | Stream MD5                                   |

`qhufdel_payload_stream_decode()` returns the number of values written, zero when complete, or a negative error code. `M_SUCCESS` is zero and `M_ERROR` is one for the other library functions.

A glue-level `qhufdel_apply()` commonly returns:

|         Code | Meaning                                                          |
| -----------: | ---------------------------------------------------------------- |
|          `0` | Decode succeeded; CRC-32 is valid                                |
| `-1` to `-3` | Package is too short, magic is invalid, or header parsing failed |
|         `-4` | Header does not match the installed model                        |
|         `-5` | Codebook is invalid or unreadable                                |
| `-6` to `-9` | Payload, padding, or final CRC could not be read/decoded         |
|        `-10` | CRC-32 mismatch                                                  |
|        `-11` | MD5 mismatch                                                     |

Any nonzero result means the new model must not be activated. Codes `-7` and `-8` may indicate a partially written model, which must be discarded.

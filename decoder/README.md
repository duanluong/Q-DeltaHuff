# Q-HUFDEL Decoder

This directory contains the portable C decoder for Q-HUFDEL update packages.
The same decoder sources are used by the host CLI and the embedded STM32F411
and ESP32-S3 examples.

The library decodes the Huffman sections of a package. Applying the decoded
delta to a model, checking the package CRC, and reading from a file or device
are the responsibility of the application.

## Getting Started

### Files

```text
decoder/
├── include/
│   ├── decoder.h       Public decoder API
│   ├── bit_manager.h   Bit reader and writer API
│   └── Types.h         Public types and package constants
├── src/
│   ├── decoder.c
│   └── bit_manager.c
└── tests/              Host-side Unity tests
```

The decoder has no operating-system dependency and does not allocate memory.
The implementation currently includes `stdio.h` for error messages from the
payload functions, so applications that cannot use `printf` should provide an
appropriate platform adaptation before linking it into a firmware target.

### Build as part of an application

Compile the two source files and add the public include directory:

```sh
gcc -std=c11 -Wall -Wextra \
	-Idecoder/include \
	decoder/src/decoder.c \
	decoder/src/bit_manager.c \
	your_application.c \
	-o your_application
```

For CMake, the equivalent library target is:

```cmake
add_library(qhufdel_decoder STATIC
	decoder/src/decoder.c
	decoder/src/bit_manager.c
)

target_include_directories(qhufdel_decoder PUBLIC decoder/include)
target_compile_features(qhufdel_decoder PUBLIC c_std_11)
```

The ESP-IDF component uses the same two source files and exposes
`decoder/include` as its include directory. The STM32 project can add the
same sources and include directory to its existing build.

## Package Format

All multi-byte integer fields are little-endian. The package is assembled from
8-byte frames and has this layout:

| Offset | Size | Section |
|---:|---:|---|
| `0` | 8 bytes | Magic: ASCII `Q-HUFDEL` |
| `8` | 8 bytes | Version, update type, model versions, and symbol count |
| `16` | 8 bytes | Code-length and symbol-width metadata |
| `24` | 16 bytes | MD5 checksum of the target model |
| `40` | 8 bytes | Target model size and meaningful payload length in bits |
| `48` | variable | Huffman histogram and symbol table |
| ... | variable | Encoded payload bitstream, frame padded |
| final 4 bytes | 4 bytes | CRC-32 of all preceding package bytes |

The fixed header is therefore 48 bytes: the 8-byte magic followed by the
40-byte `qhufdel_package_header_t`.

### Header fields

`qhufdel_package_header_t` contains:

- `version_type` and `update_type`: package metadata.
- `version_current` and `version_update`: source and target model versions.
- `num_symbols`: number of symbols in the canonical Huffman codebook.
- `min_code_len` and `max_code_len`: inclusive code-length range.
- `data_bit_width` and `symbol_bit_width`: encoder metadata, normally 8 and 9.
- `special_symbol`: the signed escape symbol used for literal bytes.
- `checksum`: 16-byte MD5 of the target model.
- `model_size`: expected target model size in bytes.
- `payload_size`: meaningful payload length in bits, excluding frame padding.

The codebook section starts immediately after the fixed header:

1. One histogram byte for every code length from `min_code_len` through
   `max_code_len`.
2. `num_symbols` signed 9-bit values. Each value uses one sign bit and an
   8-bit magnitude, ordered by code length and then symbol value.
3. Zero padding to the next 8-byte frame boundary.

The payload follows the codebook section. A normal Huffman symbol produces a
signed decoded value. When the symbol equals `special_symbol`, the decoder
reads the following 8 bits as a literal byte in the range `0..255`.

## Decode Workflow

An application normally performs these steps:

1. Read enough data to verify the first 8 bytes with
   `qhufdel_magic_code_verify()`.
2. Decode the 40-byte header at offset
   `QHUFDEL_PACKAGE_MAGIC_CODE_SIZE` with
   `qhufdel_package_header_decode()`.
3. Build the canonical Huffman table and symbol list with
   `qhufdel_huffman_codebook_regen()`.
4. Calculate the payload offset from the header and symbol count.
5. Decode the payload with either the in-memory or streaming API.
6. Apply the decoded values to the current model.
7. Verify the package CRC and compare the restored model with the target.

The decoder does not perform steps 6 and 7. The `third-party/crc32` library
can be used for the CRC-32 check.

## Public API

### Package and codebook functions

| Function | Purpose |
|---|---|
| `qhufdel_magic_code_verify()` | Check the `Q-HUFDEL` magic prefix. |
| `qhufdel_package_header_decode()` | Decode the fixed 40-byte header. |
| `qhufdel_huffman_table_build()` | Read the code-length histogram and build canonical code ranges. |
| `qhufdel_huffman_symbol_decode()` | Decode the signed 9-bit symbol list using an existing table. |
| `qhufdel_huffman_codebook_regen()` | Build the table and decode the symbol list in one call. |

`qhufdel_huffman_codebook_regen()` is the usual entry point for a complete
codebook. The caller must provide an `int16_t` array with capacity for at least
`QHUFDEL_HUFFMAN_SYMBOLS_NUM_MAX_SIZE` values, or validate the header's
`num_symbols` before allocating a smaller array.

### Payload functions

Use `qhufdel_payload_decode()` when the complete payload is already in memory.
Use the streaming API when the package is read from a file, flash, SD card, or
network in chunks:

```c
qhufdel_payload_stream_t stream;
uint8_t input_chunk[512];
qhufdel_restored_data_t output_chunk[256];

qhufdel_payload_stream_init(
	&stream,
	input_chunk,
	sizeof(input_chunk),
	fill_payload_chunk,
	fill_context,
	&table,
	symbol_table,
	header.special_symbol,
	header.model_size);

for (;;) {
	int32_t count = qhufdel_payload_stream_decode(
		&stream, output_chunk, 256);
	if (count <= 0) {
		/* count == 0 means complete; a negative value means decode failure. */
		break;
	}
	apply_decoded_values(output_chunk, (uint32_t) count);
}
```

The `fill` callback must copy the next payload bytes into the supplied buffer
and return the number of bytes copied. Returning zero signals end-of-stream.
The callback must not skip, repeat, or reorder bytes.

`qhufdel_restored_data_t.value` contains the decoded value. For ordinary
Huffman symbols, `is_changed` is `true`; for a literal-byte escape, the value
is the raw byte and `is_changed` is `false`.

### Streaming return values

`qhufdel_payload_stream_decode()` returns:

- A positive number of values written to the output buffer.
- `0` when all `total_values` have been decoded.
- `-1` when the input ends before a complete symbol is available.
- `-2` for a code that does not match the codebook.
- `-3` when an escape symbol is not followed by a complete literal byte.
- `-5` for invalid parameters.

## In-Memory Payload Example

The required buffers and call sequence are:

```c
qhufdel_huffman_table_t table;
int16_t symbols[QHUFDEL_HUFFMAN_SYMBOLS_NUM_MAX_SIZE];

if (qhufdel_huffman_codebook_regen(
		package,
		package_len,
		codebook_offset,
		header.min_code_len,
		header.max_code_len,
		&table,
		symbols,
		QHUFDEL_HUFFMAN_SYMBOLS_NUM_MAX_SIZE) != M_SUCCESS) {
	return DECODE_ERROR;
}

int32_t count = qhufdel_payload_decode(
	package,
	package_len,
	payload_offset,
	header.payload_size,
	&table,
	symbols,
	header.special_symbol,
	restored,
	restored_capacity);
```

The caller must ensure that `payload_offset` and the meaningful payload length
are within the package before calling the function. The meaningful length must
exclude the zero padding used to complete the final 8-byte frame.

## Bit Manager

`bit_manager.h` provides the MSB-first bit reader used by the decoder. It has
two modes:

- `bit_reader_init()` reads from a contiguous memory buffer.
- `bit_reader_init_stream()` refills a caller-owned chunk through a callback.

The writer functions, `bit_writer_init()`, `bit_writer_put_bits()`, and
`bit_writer_flush()`, are provided for tests and low-level format tooling.

## Limits and Known Issues

- `QHUFDEL_HUFFMAN_MAX_CODE_LEN` is 32 bits.
- `QHUFDEL_HUFFMAN_SYMBOLS_NUM_MAX_SIZE` is 512 symbols.
- The library expects callers to perform package-level bounds and CRC checks.
- `qhufdel_payload_decode()` is retained for the in-memory API, but the current
  implementation has an off-by-one output-loop issue. Prefer the streaming API
  for production use, or fix and test the in-memory path before relying on it.
- The decoder prints payload errors with `printf`; embedded integrations that
  require silent or deferred diagnostics should adapt this behavior.

## Tests

The unit tests use Unity and are intended to run from `decoder/tests` in a
POSIX-like shell with GCC:

```sh
make -C decoder/tests run_test
```

The tests cover magic validation, header parsing, canonical table generation,
symbol decoding, and package checksum behavior. They are host-side tests and
do not require an embedded SDK.

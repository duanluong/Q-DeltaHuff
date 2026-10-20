# Q-HUFDEL Binary Package Format

This format is shared by the Python tools and portable C decoder. Packages contain either target bytes (`UT=0`) or a Huffman-coded delta (`UT=1`). Validation, delta application, and safe activation belong to the application.

The current [package writer](../encoder/do_compress.py), [C types](../decoder/include/Types.h), and [C decoder](../decoder/src/decoder.c) are the implementation references.

## 1. Layout

```text
48-byte header
delta mode only: aligned Huffman table
payload and zero padding
4-byte little-endian CRC-32
```

The total package size is a multiple of eight bytes. All multi-byte headerfields and CRC bytes are little-endian. Symbol and payload bits are MSB-first.
The MD5 field uses the standard 16-byte digest order.

Full-mode payloads start at offset 48 and contain the target bytes. Delta-mode payloads follow the aligned table and contain canonical Huffman codes. The final CRC covers every preceding byte, including padding.

## 2. Header (48 bytes)

Offsets are zero-based:

| Offset | Size | Field        | Meaning                                               |
| -----: | ---: | ------------ | ----------------------------------------------------- |
|      0 |    8 | Magic        | ASCII `Q-HUFDEL`                                      |
|      8 |    1 | `VT`         | Application metadata                                  |
|      9 |    1 | `UT`         | `0` full, `1` delta                                   |
|     10 |    2 | `Ver1`       | Application metadata                                  |
|     12 |    2 | `Ver2`       | Application metadata                                  |
|     14 |    2 | `Number`     | Delta-mode symbol count                               |
|     16 |    1 | `MIN`        | Shortest delta Huffman code                           |
|     17 |    1 | `MAX`        | Longest delta Huffman code                            |
|     18 |    1 | `DWID`       | Model-element width; normally 8                       |
|     19 |    1 | `SWID`       | Raw symbol width; normally 9                          |
|     20 |    2 | `REF`        | Raw escape symbol                                     |
|     22 |    2 | Padding      | Zero; ignored                                         |
|     24 |   16 | Checksum     | MD5 digest of target bytes                            |
|     40 |    4 | Model size   | Target size in bytes                                  |
|     44 |    4 | Payload size | Full: bits = `8 * model_size`; delta: meaningful bits |

The wire header is equivalent to:

```text
<8sBBHHHBBBBHH16sII
```

The C reader parses fields explicitly. Do not read or write the packed in-memory C structure as wire data.

With the default C configuration, delta packages use `1..512` symbols, `SWID=2..16`, `DWID=1..15`, and `1 <= MIN <= MAX <= 32`. The Python writer uses `DWID=8`, `SWID=9`, nonempty targets, and code lengths up to 15. Applications must additionally validate versions, update type, base identity, and nonzero sizes.

## 3. Huffman table (delta mode)

The table starts at offset 48 and has no length field:

1. One count byte for every code length from `MIN` through `MAX`.
2. `Number` raw symbols, each `SWID` bits, packed MSB-first.
3. Zero padding to the next eight-byte boundary.

Counts must sum to `Number`; the first and last occupied lengths must match `MIN` and `MAX`. Symbols are grouped by code length and must remain in their serialized order. Direct symbols use sign-magnitude. `REF` is the raw escape symbol and may be absent if no literal is used.

## 4. Payload

Full mode stores `model_size` target bytes and declares `8 * model_size` meaningful bits.

Delta mode stores either:

```text
direct Huffman code
REF code + DWID-bit unsigned target literal
```

Codes and literals are contiguous and MSB-first; literals are not aligned. The decoder stops after the expected number of values. Unused payload bits and framing padding are zero. Padding is covered by CRC, but its zero value is not otherwise validated.

## 5. Sizes

Let `A(n) = 8 * ceil(n / 8)`, `D = MAX - MIN + 1`, `K = Number`, and `P` be the meaningful payload bits.

```text
delta table bytes = A(D + ceil(K * SWID / 8))
payload offset    = 48 + delta table bytes
package bytes     = payload offset + A(ceil(P / 8) + 4)
CRC offset        = package bytes - 4
```

For full mode:

```text
payload offset = 48
package bytes = 48 + A(ceil(P / 8) + 4)
CRC offset    = package bytes - 4
```

Use wide arithmetic for offsets. Trailing bytes after CRC are tolerated by the decoders but are not covered by CRC; standard writers omit them.

## 6. Checksums

The trailer is CRC-32/ISO-HDLC, compatible with `zlib.crc32()`:

```text
polynomial: 0x04C11DB7 (reflected 0xEDB88320)
initial:    0xFFFFFFFF
final XOR:  0xFFFFFFFF
"123456789": 0xCBF43926
```

The C streaming API starts with its own public seed from `crc32_start()`; do not replace it with `0xFFFFFFFF`.

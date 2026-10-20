# Q-HUFDEL Compression Algorithm

Q-HUFDEL has two modes:

- **Full (`UT=0`)**: stores the target bytes directly.
- **Delta (`UT=1`)**: subtracts a base model, then encodes the differences with canonical Huffman codes and an escape symbol (`REF`).

The Python tools create packages offline. The C library decodes them and returns values to the application; it does not choose a base model, apply updates, or activate firmware.

## 1. Input and notation

The encoder reads binary files or one initialized byte array from a C header. Header elements may be decimal, hexadecimal, octal, or binary literals in `[-255, 255]`; their low byte is encoded. Other header layouts are rejected. Compression is lossless for the input bytes; quantization is outside this process.

The normal configuration is `DWID=8`, `SWID=9`, and maximum code length 15. `model_size` is the target length in bytes.

## 2. Transform the input

Full mode writes:

```text
target[0], target[1], ..., target[N-1]
```

Its payload size is `8 * N` bits.

In delta mode:

```text
base[i] = existing base byte, or 0 if i is beyond the base
value[i] = target[i] - base[i]
```

The difference is a signed value in `[-255, 255]`; it is not a difference between adjacent target bytes. A base longer than the target is truncated.

## 3. Select symbols and `REF`

Delta mode may replace rare differences with `REF` plus the target byte as an unsigned literal. The encoder sorts distinct differences by increasing frequency, then value. For each candidate merge rank, it combines the rare values into one representative symbol, builds a canonical Huffman table, and computes:

```text
bits = sum(frequency * code_length)
       + frequency_of_escaped_values * (REF_code_length + DWID)
```

The valid candidate with the fewest bits wins; ties keep the smaller rank. Candidates exceeding 15-bit codes or 255 symbols per length are invalid. The representative raw symbol becomes `REF`. Every merged value is encoded as `REF` followed by its target byte. An empty target is rejected.

## 4. Symbol representation

Direct delta symbols use sign-magnitude:

```text
raw(v) = v                         if v >= 0
         2^(SWID-1) + abs(v)       if v < 0
```

With `SWID=9`, this represents `[-255, 255]`. `REF` is compared as a raw symbol before conversion to a signed value.

The literal following `REF` is the absolute target byte, not a signed difference and not sign-magnitude. It is packed immediately after the code and may cross a byte boundary.

## 5. Canonical Huffman table

The encoder repeatedly merges the two least-frequent nodes; leaf depth is the code length. Symbols are serialized by increasing code length, then signed value, then raw value. The decoder must preserve this order.

If `c[j]` is the number of codes of length `j`, the first code is:

```text
C[1] = 0
C[j] = (C[j-1] + c[j-1]) << 1
```

The `k`-th symbol of length `j` receives `C[j] + k`, represented in `j` bits. For more than one symbol the tree must be complete:

```text
sum(c[j] * 2^(-j)) = 1
```

Oversubscribed and incomplete trees are rejected, except for the single-symbol case. The package stores only the length counts and ordered raw symbols, not the tree or explicit codes.

## 6. Payload and reconstruction

Full mode writes target bytes directly. Delta mode writes either a direct Huffman code or `REF` followed by the `DWID`-bit target literal. All bits are MSB-first; literals are not byte-aligned. Unused bits and framing padding are zero.

There is no end marker. Decoding stops after:

```text
8 * model_size / DWID
```

> values, which equals `model_size` for `DWID=8`. A direct value is added to the corresponding base byte. A value after `REF` replaces the base byte with the literal.

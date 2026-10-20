import argparse
import json
import math
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Tuple
import hashlib
import numpy as np

import do_prepare
from do_huffman_canonical import (
    ORIGINAL_BITS,
    _to_python,
    find_optimal_threshold,
)

# ─────────────────────────────────────────────────────────────
#  Format constants
# ─────────────────────────────────────────────────────────────

MAGIC = b"Q-HUFDEL"  # 8 bytes — frame 1
VERSION = 1
UPDATE = 1
RWID = 8  # bit-width of replaced values
SWID = 9  # bit-width of symbol values
CUR_VER = 0
UPD_VER = 0
HEADER_FRAME_COUNT = 6
HEADER_SIZE = HEADER_FRAME_COUNT * 8  # 48 bytes


# ─────────────────────────────────────────────────────────────
#  I/O utilities
# ─────────────────────────────────────────────────────────────


def extract_name_and_id(path: Path) -> Tuple[str, str]:
    """
    Extract a model name and version ID from the header filename.
    Example: "LeNet_9-quantized.h" → ("LeNet", "9")
    """
    filename = path.name
    name_part, rest = filename.split("_", 1)
    number = rest.split("-")[0]
    return name_part, number


def _output_filename(magic: bytes, name: str, id1: str, id2: str) -> str:
    """Build the standard output binary filename."""
    return f"{magic.decode(errors='ignore')}_{name}_{id1}_{id2}.bin"


# ─────────────────────────────────────────────────────────────
#  STEP 0 — Huffman configuration
# ─────────────────────────────────────────────────────────────


def prepare_configuration(
    name: str,
    model1_path: Path,
    model1_version: int,
    model2_path: Path,
    model2_version: int,
    output_path: Path = None,
    merge_from: int = 256,
    merge_to: int = None,
) -> dict:
    result = do_prepare.single_pair_processing(model1_path, model2_path)

    data = result.to_numpy()
    value_dict = data[:, 0]  # first column = values
    freq = data[:, 1]  # second column = frequencies

    search_end = len(value_dict) if merge_to is None else merge_to
    if merge_from < 0 or search_end > len(value_dict) or merge_from >= search_end:
        raise ValueError(
            f"Merge range must satisfy 0 <= merge_from < merge_to <= {len(value_dict)}."
        )
    best_encoding_idx, best_encoding_avg, best_encoding_metadata = (
        find_optimal_threshold(value_dict, freq, merge_from, search_end, 1)
    )

    temp = {
        "original_bits": ORIGINAL_BITS,
        "merge_from": merge_from,
        "merge_to": search_end,
        "best_threshold": int(best_encoding_idx),
        "best_avg_bits": float(best_encoding_avg),
        "best_encoding": _to_python(best_encoding_metadata),
    }

    best = temp["best_encoding"]
    result = {
        "merge_from": temp["merge_from"],
        "merge_to": temp["merge_to"],
        "best_threshold": temp["best_threshold"],
        "best_avg_bits": temp["best_avg_bits"],
        "metadata_encoding": {
            "huffman_codes": best["huffman_codes"],
            "representative_value": best["representative_value"],
            "bitslength": best["bitsLen_symNum"],
            "merged_values": best["merged_values"],
        },
        "metadata_header": {
            "V_T": VERSION,
            "U_T": UPDATE,
            "current_ver": model1_version,
            "update_ver": model2_version,
            "nb_symbols": len(best["huffman_codes"]),
            "min_code_length": best["min_code_length"],
            "max_code_length": best["max_code_length"],
            "rwid": RWID,
            "swid": SWID,
            "representative_value": best["representative_value"],
        },
    }
    result["metadata_encoding"]["reverse_codes"] = {
        v: int(k) for k, v in best["huffman_codes"].items()
    }

    avg_bits = temp["best_avg_bits"]
    print(f"[config]  total symbols in Huffman table : {len(best['huffman_codes'])}")
    print(f"[config]  merged (rare) values           : {len(best['merged_values'])}")
    print(
        f"[config]  best avg bits/symbol           : {avg_bits:.4f}  "
        f"(vs 8 raw bits → {avg_bits / 8 * 100:.1f}% of original)"
    )
    return result


# ─────────────────────────────────────────────────────────────
#  STEP 2 — Build payload (Huffman-encoded delta bitstream)
# ─────────────────────────────────────────────────────────────


def build_payload(data1: np.ndarray, data2: np.ndarray, cfg: dict) -> bytes:
    """
    Compute per-byte deltas between model2 and model1, encode with the
    Huffman table, pad to 8-byte frames, and return the raw bytes
    (CRC placeholder = 0x00000000 at the end).
    """
    # Update config with actual target size and representative symbol code
    cfg["metadata_header"]["target_size"] = data2.shape[0]
    huffman_codes = cfg["metadata_encoding"]["huffman_codes"]
    representative_sym = cfg["metadata_header"]["representative_value"]
    fallback_code = huffman_codes.get(str(representative_sym), "")

    deltas = data2.astype(np.int16) - data1.astype(np.int16)
    assert len(deltas) == len(data2.flat), "Length mismatch between delta and model2"

    # ── Build raw bitstring ───────────────────────────────────────────────────
    bits = []
    huffman_hits = 0
    fallback_hits = 0

    for d, w2 in zip(deltas, data2.flat):
        key = str(int(d))
        if key in huffman_codes and key != str(representative_sym):
            bits.append(huffman_codes[key])
            huffman_hits += 1
        else:
            bits.append(fallback_code)
            bits.append(f"{int(w2) & 0xFF:08b}")
            fallback_hits += 1

    bitstring = "".join(bits)
    print(
        f"[payload] huffman hits : {huffman_hits:,}  |  fallback hits : {fallback_hits:,}"
    )

    # Update config with actual payload bit length before padding
    cfg["metadata_header"]["payload_bits_len"] = len(bitstring)

    # ── Determine frame count and pad ─────────────────────────────────────────
    raw_bytes = math.ceil(len(bitstring) / 8)
    total_bytes = raw_bytes + 4  # reserve 4 bytes for CRC
    nb_frames = math.ceil(total_bytes / 8)
    target_bits = (nb_frames * 8 * 8) - 32  # total bits minus 32 CRC bits

    pad = target_bits - len(bitstring)
    bitstring += "0" * pad

    payload = bytes(int(bitstring[i : i + 8], 2) for i in range(0, len(bitstring), 8))

    # CRC placeholder (4 zero bytes appended)
    package = bytes(bytearray(payload) + bytearray(4))

    assert len(package) % 8 == 0, "Package must be a multiple of 8 bytes"
    assert len(package) == nb_frames * 8, "Package size must equal n * 8 bytes"
    assert len(package) >= len(payload) + 4, "CRC bytes must fit within package"

    print(f"[payload] encoded size : {len(package):,} bytes  ({nb_frames} frames)")
    return package


# ─────────────────────────────────────────────────────────────
#  STEP 3 — Build symbol table section
# ─────────────────────────────────────────────────────────────


def encode_sym9(value: int) -> str:
    """Encode a symbol value (-255..255) as a 9-bit sign-magnitude bitstring."""
    if not (-255 <= value <= 255):
        raise ValueError(f"Symbol value {value} out of range -255..255")
    sign = 1 if value < 0 else 0
    mag = abs(value)
    return f"{sign}{mag:08b}"


def decode_sym9(bits9: str) -> int:
    """Decode a 9-bit sign-magnitude bitstring back to an integer."""
    sign = int(bits9[0])
    mag = int(bits9[1:], 2)
    return -mag if sign else mag


def symbol_section_size_from_counts(n_lengths: int, n_symbols: int) -> int:
    """Byte size of the symbol section (always a multiple of 8)."""
    total_bits = n_lengths * 8 + n_symbols * 9
    return math.ceil(total_bits / 64) * 8


def build_symbol_table(cfg: dict) -> bytes:
    """
    Encode histogram counts and symbol values into a single contiguous
    bitstream, padded to a multiple of 8 bytes.

    Layout:
        Section A: count per code-length (8 bits each, ascending length order)
        Section B: symbol values         (9 bits each, sign-magnitude,
                                          sorted by code-length then value)
        Padding:   zero-pad to next 8-byte boundary
    """
    bitslen_symnum = cfg["metadata_encoding"]["bitslength"]
    huffman_codes = cfg["metadata_encoding"]["huffman_codes"]
    bits = []

    # Section A — histogram counts
    filled_codelen = {
        str(k): bitslen_symnum.get(str(k), 0)
        for k in range(min(map(int, bitslen_symnum)), max(map(int, bitslen_symnum)) + 1)
    }
    sorted_lengths = sorted(filled_codelen.keys(), key=int)
    for length_key in sorted_lengths:
        bits.append(f"{filled_codelen[length_key]:08b}")

    # Section B — symbol values
    sorted_symbols = sorted(
        huffman_codes.keys(), key=lambda k: (len(huffman_codes[k]), int(k))
    )
    for sym_key in sorted_symbols:
        bits.append(encode_sym9(int(sym_key)))

    # Pad to 8-byte boundary
    bitstring = "".join(bits)
    pad = (64 - len(bitstring) % 64) % 64
    bitstring += "0" * pad

    encoded = bytes(int(bitstring[i : i + 8], 2) for i in range(0, len(bitstring), 8))
    print(f"[symtab]  encoded size : {len(encoded):,} bytes")
    return encoded


# ─────────────────────────────────────────────────────────────
#  STEP 4 — Build package header
# ─────────────────────────────────────────────────────────────


@dataclass
class HufDelHeader:
    # Frame 2
    version_type: int  # V-T  (1 byte)
    update_type: int  # U-T  (1 byte)
    version_current: int  # current model version (2 bytes LE)
    version_update: int  # update model version  (2 bytes LE)
    num_huffman_symbols: int  # symbol count         (2 bytes LE)
    # Frame 3
    min_code_len: int  # 1 byte
    max_code_len: int  # 1 byte
    replaced_bits: int  # bit-width of replaced values (e.g. 8)
    symbol_bits: int  # bit-width of symbol values   (e.g. 9)
    special_symbol: int  # fallback / merged-values token (2 bytes)
    # Frames 4–5: MD5 checksum (16 bytes stored as two 8-byte LE quads)
    checksum: bytes
    # Frame 6
    target_size_bytes: int  # model2 file size in bytes (4 bytes LE)
    bitstream_size_bits: int  # total bitstream length in bits (4 bytes LE)


def encode_header(h: HufDelHeader) -> bytes:
    """
    Encode the 6-frame (48-byte) package header.

    Frame layout:
        [0]  Magic        "Q-HUFDEL"                              8 bytes
        [1]  V-T | U-T | ver_current (LE,2) | ver_update (LE,2)
             | n_symbols (LE,2)                                   8 bytes
        [2]  min_code | max_code | replaced_bits | symbol_bits
             | special_symbol (LE,2) | _pad (2)                   8 bytes
        [3]  Checksum bytes  0..7   (little-endian quad)          8 bytes
        [4]  Checksum bytes  8..15  (little-endian quad)          8 bytes
        [5]  target_size (LE,4) | bitstream_size (LE,4)           8 bytes
    """
    if len(h.checksum) != 16:
        raise ValueError("Checksum must be exactly 16 bytes for the package header")

    frame1 = MAGIC

    frame2 = struct.pack(
        "<BBHHH",
        h.version_type,
        h.update_type,
        h.version_current,
        h.version_update,
        h.num_huffman_symbols,
    )

    frame3 = struct.pack(
        "<BBBBh2x",
        h.min_code_len,
        h.max_code_len,
        h.replaced_bits,
        h.symbol_bits,
        h.special_symbol,
    )

    f1_int = int.from_bytes(h.checksum[:8], byteorder="big")
    f2_int = int.from_bytes(h.checksum[8:], byteorder="big")
    frame4 = struct.pack("<QQ", f1_int, f2_int)

    frame5 = struct.pack(
        "<II",
        h.target_size_bytes,
        h.bitstream_size_bits,
    )

    header = frame1 + frame2 + frame3 + frame4 + frame5
    assert len(header) == HEADER_SIZE, f"Header must be {HEADER_SIZE} bytes"
    return header


def build_header(header: dict) -> bytes:
    """Construct and encode the package header from config metadata."""
    h = HufDelHeader(
        version_type=header["V_T"],
        update_type=header["U_T"],
        version_current=header["current_ver"],
        version_update=header["update_ver"],
        num_huffman_symbols=header["nb_symbols"],
        min_code_len=header["min_code_length"],
        max_code_len=header["max_code_length"],
        replaced_bits=header["rwid"],
        symbol_bits=header["swid"],
        special_symbol=header["representative_value"],
        checksum=bytes.fromhex(header["target_checksum"]),
        target_size_bytes=header["target_size"],
        bitstream_size_bits=header["payload_bits_len"],
    )

    encoded = encode_header(h)
    print(f"[header]  encoded size : {len(encoded):,} bytes")
    return encoded


# ─────────────────────────────────────────────────────────────
#  STEP 5 — Assemble package and compute CRC-32
# ─────────────────────────────────────────────────────────────


def update_crc32(data: bytes) -> bytes:
    """
    Replace the last 4 bytes of `data` with the CRC-32 of all preceding bytes.
    """
    if len(data) < 4:
        raise ValueError("Data must be at least 4 bytes long")
    payload = data[:-4]
    crc = zlib.crc32(payload) & 0xFFFFFFFF
    crc_bytes = struct.pack("<I", crc)
    return payload + crc_bytes


def assemble_package(header: bytes, symbol_table: bytes, payload: bytes) -> bytes:
    """Concatenate the three sections and write the final CRC-32."""
    chunks = [header, symbol_table, payload]
    package = bytearray().join(bytearray(c) for c in chunks)
    assert len(package) % 8 == 0, "Assembled package must be a multiple of 8 bytes"

    package = update_crc32(bytes(package))

    # Verify
    stored = struct.unpack("<I", package[-4:])[0]
    computed = zlib.crc32(package[:-4]) & 0xFFFFFFFF
    assert stored == computed, "CRC-32 verification failed!"
    print(f"[package] total size   : {len(package):,} bytes")
    print(f"[package] CRC-32       : {stored:#010x}  ✓")
    return package


# ─────────────────────────────────────────────────────────────
#  Pipeline entry point
# ─────────────────────────────────────────────────────────────


def do_compress(
    name: str,
    header1_path: Path,
    version_1: int,
    header2_path: Path,
    version_2: int,
    output_path: Path = None,
    output_filename: str = None,
    merge_from: int = 256,
    merge_to: int = None,
    generate_json_report: bool = False,
) -> str:
    base = header1_path.parent if output_path is None else output_path
    filename = output_filename or _output_filename(MAGIC, name, version_1, version_2)
    out_file = base / filename
    print(f"  [info] Output package will be: {out_file.name}")

    # 0. Load Huffman configuration
    print("\n── Initializing config ──────────────────────────────────────")
    cfg = prepare_configuration(
        name,
        header1_path,
        version_1,
        header2_path,
        version_2,
        base,
        merge_from,
        merge_to,
    )
    print(
        f"[config] configuration of compressing: {cfg['metadata_header']['current_ver']} -> {cfg['metadata_header']['update_ver']}"
    )

    # 1. Parse C header files
    print("\n── Parsing model headers ───────────────────────────────")
    name1, data1 = do_prepare.parse_c_header(header1_path)
    name2, data2 = do_prepare.parse_c_header(header2_path)
    print(f"  model1 : {name1}  ({data1.shape[0]:,} bytes)")
    print(f"  model2 : {name2}  ({data2.shape[0]:,} bytes)")

    # The checksum covers the extracted model bytes
    md5_hex = hashlib.md5(data2.tobytes()).hexdigest()
    cfg["metadata_header"]["target_checksum"] = md5_hex

    # 2. Build payload
    print("\n── Building payload ─────────────────────────────────────")
    payload = build_payload(data1, data2, cfg)

    # 3. Build symbol table
    print("\n── Building symbol table ────────────────────────────────")
    symbol_table = build_symbol_table(cfg)

    # 4. Build header
    print("\n── Building header ──────────────────────────────────────")
    header = build_header(cfg["metadata_header"])

    # 5. Assemble and write
    print("\n── Assembling package ───────────────────────────────────")
    package = assemble_package(header, symbol_table, payload)

    out_file.parent.mkdir(parents=True, exist_ok=True)
    with open(out_file, "wb") as f:
        f.write(package)

    if generate_json_report:
        report_file = out_file.with_suffix(".json")
        report = {
            "output_file": out_file.name,
            "merge_from": merge_from,
            "merge_to": merge_to,
            "best_threshold": cfg["best_threshold"],
            "best_avg_bits": cfg["best_avg_bits"],
            "configuration": _to_python(cfg),
        }
        with open(report_file, "w") as f:
            json.dump(report, f, indent=2)
        print(f"[report]  saved to: {report_file}")

    print(f"\n✓  Package written to: {out_file}")
    return str(out_file)

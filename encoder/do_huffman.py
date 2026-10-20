"""
Canonical Huffman Compression
==============================
Input: list of symbols (numbers) and corresponding list of frequencies.
Produces canonical Huffman codes — a standard form where codes of the same
length are assigned in sorted symbol order, making the codebook easily
reconstructable from just (symbol, code_length) pairs.
"""

import heapq
from dataclasses import dataclass, field
from typing import Any


# ─────────────────────────────────────────────
# 1. Build the Huffman tree
# ─────────────────────────────────────────────


@dataclass(order=True)
class HuffNode:
    freq: int
    symbol: Any = field(compare=False, default=None)
    left: "HuffNode | None" = field(compare=False, default=None)
    right: "HuffNode | None" = field(compare=False, default=None)

    @property
    def is_leaf(self) -> bool:
        return self.left is None and self.right is None


def build_huffman_tree(symbols: list, freqs: list[int]) -> HuffNode:
    """Build a Huffman tree using a min-heap."""
    if len(symbols) != len(freqs):
        raise ValueError("symbols and freqs must have the same length.")
    if len(symbols) == 0:
        raise ValueError("Input lists must not be empty.")

    heap: list[HuffNode] = [HuffNode(f, s) for s, f in zip(symbols, freqs)]
    heapq.heapify(heap)

    # Edge case: single symbol
    if len(heap) == 1:
        only = heapq.heappop(heap)
        return HuffNode(only.freq, left=only)

    while len(heap) > 1:
        left = heapq.heappop(heap)
        right = heapq.heappop(heap)
        merged = HuffNode(left.freq + right.freq, left=left, right=right)
        heapq.heappush(heap, merged)

    return heap[0]


# ─────────────────────────────────────────────
# 2. Compute code lengths from the tree
# ─────────────────────────────────────────────


def get_code_lengths(root: HuffNode) -> dict:
    """
    Traverse the tree and record the depth (= code length) of every leaf.
    Returns {symbol: code_length}.
    """
    lengths: dict = {}

    def _traverse(node: HuffNode, depth: int):
        if node is None:
            return
        if node.is_leaf:
            lengths[node.symbol] = depth
            return
        _traverse(node.left, depth + 1)
        _traverse(node.right, depth + 1)

    _traverse(root, 0)
    return lengths


# ─────────────────────────────────────────────
# 3. Build canonical codes from lengths
# ─────────────────────────────────────────────


def build_canonical_codes(code_lengths: dict) -> dict:
    """
    Given {symbol: code_length}, produce canonical binary codes.

    Algorithm:
      1. Sort symbols: primary key = code_length, secondary key = symbol value.
      2. Assign codes starting from 0, incrementing by 1 each step,
         left-shifting when the code length increases.

    Returns {symbol: code_string}.
    """
    # Sort by (length, symbol)
    sorted_symbols = sorted(code_lengths.items(), key=lambda x: (x[1], x[0]))

    canonical: dict = {}
    code = 0
    prev_length = 0

    for symbol, length in sorted_symbols:
        if prev_length != 0:
            # Shift left by the difference in lengths
            code <<= length - prev_length
        canonical[symbol] = format(code, f"0{length}b")
        code += 1
        prev_length = length

    return canonical


# ─────────────────────────────────────────────
# 4. High-level encode / decode
# ─────────────────────────────────────────────


def encode(symbols: list, freqs: list[int]) -> dict:
    """
    Full pipeline: symbols + freqs → canonical Huffman codebook.

    Returns a dict with:
      'codes'        : {symbol: canonical_binary_string}
      'code_lengths' : {symbol: int}
      'avg_bits'     : float  (weighted average code length)
      'entropy'      : float  (Shannon entropy in bits)
    """
    import math

    tree = build_huffman_tree(symbols, freqs)
    code_lengths = get_code_lengths(tree)
    codes = build_canonical_codes(code_lengths)

    total_freq = sum(freqs)
    avg_bits = sum(code_lengths[s] * f for s, f in zip(symbols, freqs)) / total_freq
    entropy = -sum((f / total_freq) * math.log2(f / total_freq) for f in freqs if f > 0)

    return {
        "codes": codes,
        "code_lengths": code_lengths,
        "avg_bits": avg_bits,
        "entropy": entropy,
    }


def encode_sequence(sequence: list, codes: dict) -> str:
    """Encode a sequence of symbols to a binary string using the codebook."""
    return "".join(codes[s] for s in sequence)


def decode_sequence(bitstring: str, codes: dict) -> list:
    """
    Decode a binary string back to symbols.
    Uses a reverse lookup; works for any prefix-free code.
    """
    reverse = {v: k for k, v in codes.items()}
    result, buf = [], ""
    for bit in bitstring:
        buf += bit
        if buf in reverse:
            result.append(reverse[buf])
            buf = ""
    if buf:
        raise ValueError(f"Leftover bits that don't match any code: '{buf}'")
    return result


# ─────────────────────────────────────────────
# 5. Pretty-print helper
# ─────────────────────────────────────────────


def print_codebook(result: dict, freqs: list[int], symbols: list):
    total = sum(freqs)
    freq_map = dict(zip(symbols, freqs))

    print(f"\n{'Symbol':>8}  {'Freq':>6}  {'Prob':>7}  {'Len':>4}  {'Code'}")
    print("─" * 48)
    for sym, code in sorted(
        result["codes"].items(), key=lambda x: result["code_lengths"][x[0]]
    ):
        f = freq_map[sym]
        print(
            f"{sym:>8}  {f:>6}  {f/total:>7.4f}  "
            f"{result['code_lengths'][sym]:>4}  {code}"
        )

    print("─" * 48)
    print(f"  Average code length : {result['avg_bits']:.4f} bits/symbol")
    print(f"  Shannon entropy     : {result['entropy']:.4f} bits/symbol")
    print(
        f"  Compression ratio   : {result['entropy']/result['avg_bits']:.4f}"
        f"  (entropy / avg_len)\n"
    )


# ─────────────────────────────────────────────
# 6. Demo
# ─────────────────────────────────────────────

if __name__ == "__main__":
    # ── Example 1: classic textbook example ──
    symbols = [1, 2, 3, 4, 5, 6]
    freqs = [45, 13, 12, 16, 9, 5]

    print("=" * 48)
    print("  Example 1 — Classic Huffman")
    print("=" * 48)
    result = encode(symbols, freqs)
    print_codebook(result, freqs, symbols)

    # ── Example 2: encode / decode a sequence ──
    sequence = [1, 1, 2, 3, 1, 4, 5, 6, 2]
    bitstring = encode_sequence(sequence, result["codes"])
    decoded = decode_sequence(bitstring, result["codes"])

    print(f"  Original  : {sequence}")
    print(f"  Encoded   : {bitstring}  ({len(bitstring)} bits)")
    print(f"  Decoded   : {decoded}")
    print(f"  Match     : {sequence == decoded}\n")

    # ── Example 3: skewed distribution ──
    symbols2 = [10, 20, 30, 40]
    freqs2 = [1, 2, 4, 8]

    print("=" * 48)
    print("  Example 2 — Skewed Distribution")
    print("=" * 48)
    result2 = encode(symbols2, freqs2)
    print_codebook(result2, freqs2, symbols2)

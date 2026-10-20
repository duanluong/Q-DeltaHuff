"""
Huffman Threshold Optimizer
============================
Finds the optimal merge-threshold index for a sorted-by-frequency value list.
Values at positions [0, index) are merged into a single sentinel token (1000),
then Huffman coding is applied to the reduced symbol set.

Bit savings are reported against a 9-bit fixed-length baseline
(1 sign bit + 8 value bits, for values in range -255..255).

CSV format:
  - Column 0 : integer values  (-255 .. 255)
  - Columns 1+: frequency counts (one column per delta model / channel)

Usage:
  python do_huffman.py --file data.csv --output ./results [--debug]
"""

import os
import json
import argparse
from datetime import datetime
import numpy as np
import pandas as pd
from typing import Dict, List, Tuple
import heapq
from dataclasses import dataclass, field
from typing import Optional


g_debug: bool = False

ORIGINAL_BITS = 8  # the original fixed-length encoding uses 8 bits because model is quantised to unsigned integers 8-bits
TABLE_OVERHEAD_SYMBOLS = (
    512  # phantom symbols modelling Huffman-table transmission cost
)
ESCAPE_BITS = 8  # bits used to signal each occurrence of merged group


@dataclass(order=True)
class HuffNode:
    """
    A single node in the Huffman binary tree.

    `freq`   – combined frequency of all symbols in this sub-tree.
    `symbol` – the actual character (None for internal/branch nodes).
    `left`   – left child  (represents bit 0).
    `right`  – right child (represents bit 1).

    The `order=True` on the dataclass lets Python compare nodes by `freq`
    first, which is exactly what the min-heap (priority queue) needs.
    Because two nodes can share the same frequency, we include a tie-breaking
    `index` field so that comparison never falls through to the children
    (HuffNode objects are not directly comparable for ordering).
    """

    freq: int
    index: int  # tie-breaker so heap stays stable
    symbol: Optional[str] = field(default=None, compare=False)
    left: Optional["HuffNode"] = field(default=None, compare=False)
    right: Optional["HuffNode"] = field(default=None, compare=False)

    @property
    def is_leaf(self) -> bool:
        """A leaf holds a real symbol; internal nodes hold None."""
        return self.symbol is not None


def build_huffman_tree(characters: list, frequencies: list) -> HuffNode:
    """
    Construct the Huffman tree from a frequency table and return its root.

    Algorithm (greedy / min-heap)
    ─────────────────────────────
    • Push every symbol as a leaf node onto a min-heap keyed by frequency.
    • Repeatedly pop the two *lowest-frequency* nodes, merge them into a
      new internal node whose frequency is their sum, and push it back.
    • Repeat until only one node remains — that is the root.

    Time complexity: O(n log n) where n is the alphabet size.
    """
    # Each entry: (freq, tie-breaker-index, HuffNode)
    counter = 0
    heap: list[HuffNode] = []
    for symbol, freq in zip(characters, frequencies):
        heapq.heappush(heap, HuffNode(freq=freq, index=counter, symbol=symbol))
        counter += 1

    # Edge case: single unique symbol → wrap in a dummy root so it gets depth 1
    if len(heap) == 1:
        only = heapq.heappop(heap)
        root = HuffNode(freq=only.freq, index=counter, left=only)
        return root

    while len(heap) > 1:
        # Pop the two nodes with the smallest frequencies
        left = heapq.heappop(heap)
        right = heapq.heappop(heap)

        # Merge: new internal node, frequency = sum of children
        merged = HuffNode(
            freq=left.freq + right.freq,
            index=counter,
            left=left,
            right=right,
        )
        counter += 1
        heapq.heappush(heap, merged)

    return heapq.heappop(heap)  # the sole remaining node is the root


def get_code_lengths(root: HuffNode) -> dict:
    """
    Depth-first traversal of the Huffman tree.
    Returns {symbol: depth} — the *length* (in bits) each symbol's code will
    occupy.  This is everything we need to build a canonical codebook.
    """
    lengths: dict[str, int] = {}

    def _dfs(node: Optional[HuffNode], depth: int) -> None:
        if node is None:
            return
        if node.is_leaf:
            # Assign at least depth 1 (handles single-symbol edge case)
            lengths[node.symbol] = max(depth, 1)
            return
        _dfs(node.left, depth + 1)
        _dfs(node.right, depth + 1)

    _dfs(root, 0)
    return lengths


def build_canonical_codebook(lengths: dict) -> dict:
    """
    Derive the canonical Huffman codes from a symbol→length mapping.

    Canonical Huffman rules
    ───────────────────────
    1. Codes of shorter length come *numerically before* codes of longer length.
    2. Within the same length, codes are assigned in *lexicographic order* of
       the symbols (alphabetical for characters).
    3. The first code of length L is determined by left-shifting the previous
       length's last code and adding 1:

           code[0 of new length] = (code[-1 of prev length] + 1) << (new_L - prev_L)

    This produces a compact, self-describing table: a decoder only needs to
    store the symbol list sorted by (length, symbol) and the starting code for
    each length — not the full tree.

    Returns {symbol: bit-string}, e.g. {'a': '0', 'b': '10', 'c': '11'}.
    """
    if not lengths:
        return {}

    # Sort symbols: first by code-length, then alphabetically (canonical order)
    sorted_symbols = sorted(lengths.items(), key=lambda kv: (kv[1], kv[0]))

    codebook: dict[str, str] = {}
    current_code = 0
    prev_length = 0

    for symbol, length in sorted_symbols:
        # Shift left by the increase in bit-length to maintain prefix-free property
        current_code <<= length - prev_length
        # Represent the integer as a zero-padded binary string of the right width
        codebook[symbol] = format(current_code, f"0{length}b")
        current_code += 1  # next symbol at same length gets the next integer
        prev_length = length

    return codebook


def canonical_huffman(
    characters: list, frequencies: list
) -> Tuple[HuffNode, dict, float]:
    """
    Build Huffman codes and compute the true weighted average code length.

    Returns
    -------
    root          : HuffmanNode
    huffman_codes : dict  {symbol -> binary string}
    avg_length    : float  weighted average bits per symbol
    """
    if len(characters) != len(frequencies):
        raise ValueError("characters and frequencies must have the same length.")
    if not characters:
        raise ValueError("Input cannot be empty.")

    root = build_huffman_tree(characters, frequencies)
    lengths = get_code_lengths(root)
    codebook = build_canonical_codebook(lengths)

    total_symbols = sum(frequencies)

    avg_length = (
        sum(len(codebook[c]) * f for c, f in zip(characters, frequencies))
        / total_symbols
    )

    return root, codebook, avg_length, lengths


def calculate_avg_with_index(
    value_dict: np.ndarray,
    frequency: np.ndarray,
    index_to_merge: int,
) -> Tuple[float, Dict]:
    """
    Merge values at sorted positions [0, index_to_merge) into the last merged symbol (sentinel), then apply Huffman coding, and return the effective average bits per symbol.

    The effective cost model accounts for:
      - Huffman table transmission overhead  (TABLE_OVERHEAD_SYMBOLS extra symbols)
      - ESCAPE_BITS bits per occurrence of the merged group (to signal it in stream)

    Parameters
    ----------
    value_dict     : array of unique integer values (unsorted)
    frequency      : array of corresponding frequencies (unsorted, same order)
    index_to_merge : number of lowest-frequency symbols to merge (exclusive upper bound)

    Returns
    -------
    total_avg   : effective average bits per symbol (lower is better)
    encoding_info : dict with full statistics
    """
    # Sort by frequency ascending (stable, so equal-freq items keep original order)
    sort_indices = np.argsort(frequency, kind="stable")
    sorted_values = value_dict[sort_indices]
    sorted_freqs = frequency[sort_indices]

    total_data = int(np.sum(sorted_freqs))

    # Build the reduced symbol list: sentinel + remaining symbols
    merged_freq = int(np.sum(sorted_freqs[:index_to_merge]))
    remaining_values = sorted_values[index_to_merge:].tolist()
    remaining_freqs = sorted_freqs[index_to_merge:].tolist()

    # The representative value can be any integer not in the original value_dict, since it's just a placeholder for the merged group. We choose the last merged symbol for interpretability, but it won't affect the Huffman structure since it's treated as a single symbol with frequency = sum of merged frequencies.
    representative_value = np.int16(sorted_values[index_to_merge - 1])

    list_dict = [representative_value] + remaining_values
    list_freq = [merged_freq] + remaining_freqs

    _, codes, avg_length, code_length = canonical_huffman(list_dict, list_freq)

    code_length = dict(sorted(code_length.items(), key=lambda item: (item[1], item[0])))
    min_bits_length = min(code_length.values())
    max_bits_length = max(code_length.values())

    bitslen_symnum: dict[str, int] = {}

    for v in code_length.values():
        bitslen_symnum[v] = bitslen_symnum.get(v, 0) + 1

    # Effective average bits per symbol:
    #   numerator   = Huffman bits for all real symbols (totalData)
    #               + Huffman bits for overhead table   (TABLE_OVERHEAD_SYMBOLS symbols)
    #               + ESCAPE_BITS * count of merged occurrences (list_freq[0])
    #   denominator = totalData   (we amortise overhead over real data)
    total_avg = ((avg_length * (total_data)) + (ESCAPE_BITS * merged_freq)) / total_data

    saving_percentage = (ORIGINAL_BITS - total_avg) / ORIGINAL_BITS * 100

    if g_debug:
        print(f"  index_to_merge     : {index_to_merge}")
        print(f"  merged symbols     : {index_to_merge}  (freq sum = {merged_freq})")
        print(f"  Huffman avg length : {avg_length:.4f} bits")
        print(f"  Total avg (model)  : {total_avg:.4f} bits")
        print(f"  Saving vs {ORIGINAL_BITS}-bit    : {saving_percentage:.2f}%")
        print(f"  Unique symbols     : {len(list_dict)}")
        print("-" * 60)

    encoding_info = {
        "index_threshold": index_to_merge,
        "merged_count": index_to_merge,
        "representative_value": representative_value,
        "merged_freq_sum": merged_freq,
        "merged_values": sorted_values[:index_to_merge].tolist(),
        "huffman_codes": codes,
        "sym_bitsLen": code_length,
        "min_code_length": min_bits_length,
        "max_code_length": max_bits_length,
        "avg_code_length": avg_length,
        "bitsLen_symNum": bitslen_symnum,
        "total_avg_bits": total_avg,
        f"saving_pct_vs_{ORIGINAL_BITS}bit": saving_percentage,
        "num_unique_symbols": len(list_dict),
        "total_samples": total_data,
    }

    return total_avg, encoding_info


def find_optimal_threshold(
    value_dict: np.ndarray,
    value_freq: np.ndarray,
    min_index: int = 1,
    max_index: int = None,
    step: int = 1,
) -> Tuple[int, float, Dict, List]:
    """
    Exhaustively search every threshold in [min_index, max_index) to find
    the one that minimises the effective average bits per symbol.

    Parameters
    ----------
    value_dict : array of unique integer values
    value_freq : array of frequencies (same order as value_dict)
    min_index  : first threshold to test (default 1 — merge at least 1 symbol)
    max_index  : exclusive upper bound (default: len(value_dict))
    step       : search step size

    Returns
    -------
    best_index, best_avg, best_info, all_results
    """
    if max_index is None:
        max_index = len(value_dict)

    if g_debug:
        print("=" * 60)
        print("SEARCHING FOR OPTIMAL THRESHOLD")
        print(f"  range [{min_index}, {max_index})  step={step}")
        print("=" * 60)

    best_index = min_index
    best_avg = float("inf")
    best_info = None

    for idx in range(min_index, max_index, step):
        avg, info = calculate_avg_with_index(value_dict, value_freq, idx)

        if avg < best_avg:
            best_avg = avg
            best_index = idx
            best_info = info

    return best_index, best_avg, best_info


def _to_python(obj):
    """Recursively convert numpy scalars / arrays to plain Python types."""
    import numpy as np

    if isinstance(obj, np.ndarray):
        return obj.tolist()

    if isinstance(obj, np.integer):
        return int(obj)

    if isinstance(obj, np.floating):
        return float(obj)

    if isinstance(obj, np.bool_):
        return bool(obj)

    if isinstance(obj, dict):
        return {
            str(_to_python(k)): _to_python(v)  # ✅ convert keys to string
            for k, v in obj.items()
        }

    if isinstance(obj, list):
        return [_to_python(v) for v in obj]

    return obj


def main():
    global g_debug  # FIX: must declare global so assignment updates the module-level flag

    parser = argparse.ArgumentParser(
        description="Find optimal Huffman merge threshold for each frequency column.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--file", "-f", required=True, help="Path to the input CSV file."
    )
    parser.add_argument(
        "--output",
        "-o",
        default="./results",
        help="Directory to save per-column JSON result files (default: ./huffman_results).",
    )
    parser.add_argument(
        "--debug", action="store_true", help="Print per-threshold debug info."
    )
    parser.add_argument(
        "--min-index",
        type=int,
        default=1,
        help="Minimum threshold index to test (default: 1).",
    )
    parser.add_argument(
        "--step",
        type=int,
        default=1,
        help="Step size for threshold search (default: 1).",
    )

    args = parser.parse_args()
    g_debug = args.debug

    df = pd.read_csv(args.file)
    columns = df.columns.tolist()
    data = df.to_numpy()
    n_cols = df.shape[1]

    print(f"Loaded : {args.file}  shape={data.shape}")
    print(f"Columns: {columns}")

    value_dict = data[:, 0]  # first column = values
    print(
        f"\nValue column : '{columns[0]}'  shape={value_dict.shape}  dtype={value_dict.dtype}"
    )

    # Create output directory based on working timestamp
    timestamp = datetime.now().strftime("%Y%m%d")
    args.output = os.path.join(args.output, timestamp)
    args.output = os.path.join(args.output, "canonical")
    os.makedirs(args.output, exist_ok=True)

    # Process each frequency column
    for col_idx in range(1, n_cols):
        col_name = columns[col_idx]
        freq = data[:, col_idx]
        total = int(np.sum(freq))

        print(f"\n{'='*60}")
        print(
            f"Column '{col_name}'  |  total samples = {total:,}"
        )  # FIX: print total (was discarded)
        print(f"{'='*60}")

        best_index, best_avg, best_info = find_optimal_threshold(
            value_dict,
            freq,
            min_index=args.min_index,
            max_index=len(value_dict),
            step=args.step,
        )

        saving = best_info[f"saving_pct_vs_{ORIGINAL_BITS}bit"]
        print(f"  Best threshold index   : {best_index}")
        print(f"  Symbols merged         : {best_info['merged_count']}")
        print(f"  Effective avg bits     : {best_avg:.4f}")
        print(f"  Saving vs {ORIGINAL_BITS}-bit fixed  : {saving:.2f}%")

        # Serialise — include ALL threshold results for plotting / analysis
        result_json = {
            "column": col_name,
            "total_samples": total,
            "original_bits": ORIGINAL_BITS,
            "best_threshold": int(best_index),
            "best_avg_bits": float(best_avg),
            "best_encoding": _to_python(best_info),
        }

        out_path = os.path.join(args.output, f"{col_name}.json")
        with open(out_path, "w") as fh:
            try:
                json.dump(result_json, fh, indent=2)
            except TypeError as e:
                print("JSON error:", e)

        print(f"  Saved → {out_path}")


if __name__ == "__main__":
    main()

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


# ──────────────────────────────────────────────────────────────────
# Global debug flag (set via CLI --debug)
# ──────────────────────────────────────────────────────────────────
g_debug: bool = False

SENTINEL_VALUE = 1000  # representative token for merged low-freq values
ORIGINAL_BITS = 8  # the original fixed-length encoding uses 8 bits because model is quantised to unsigned integers 8-bits
TABLE_OVERHEAD_SYMBOLS = (
    512  # phantom symbols modelling Huffman-table transmission cost
)
ESCAPE_BITS = 8  # bits used to signal each occurrence of merged group


class HuffmanNode:
    """A node in the Huffman binary tree."""

    __slots__ = ("char", "freq", "left", "right")

    def __init__(self, char, freq):
        self.char = char
        self.freq = freq
        self.left = None
        self.right = None

    # heapq needs nodes to be comparable
    def __lt__(self, other):
        return self.freq < other.freq


def build_huffman_tree(characters: list, frequencies: list) -> HuffmanNode:
    """Build a Huffman tree using a min-heap (O(n log n))."""
    import heapq

    heap = [HuffmanNode(c, f) for c, f in zip(characters, frequencies)]
    heapq.heapify(heap)

    while len(heap) > 1:
        left = heapq.heappop(heap)
        right = heapq.heappop(heap)
        internal = HuffmanNode(None, left.freq + right.freq)
        internal.left = left
        internal.right = right
        heapq.heappush(heap, internal)

    return heap[0]


def generate_codes(node: HuffmanNode, prefix: str = "", codes: dict = None) -> dict:
    """Recursively generate binary codes for each leaf symbol."""
    if codes is None:
        codes = {}

    if node.char is not None:
        # Single-symbol edge case: assign "0"
        codes[node.char] = prefix if prefix else "0"
        return codes

    if node.left:
        generate_codes(node.left, prefix + "0", codes)
    if node.right:
        generate_codes(node.right, prefix + "1", codes)

    return codes


def huffman_encoding(
    characters: list, frequencies: list
) -> Tuple[HuffmanNode, dict, float]:
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
    huffman_codes = generate_codes(root)

    total_symbols = sum(frequencies)

    # FIX: match each symbol to its OWN code via the dict key, not by zip position.
    # The original code used zip(huffman_codes.items(), frequencies) which paired
    # codes with frequencies by list position — incorrect when dict order ≠ input order.
    avg_length = (
        sum(len(huffman_codes[c]) * f for c, f in zip(characters, frequencies))
        / total_symbols
    )

    return root, huffman_codes, avg_length


def calculate_avg_with_index(
    value_dict: np.ndarray,
    frequency: np.ndarray,
    index_to_merge: int,
) -> Tuple[float, Dict]:
    """
    Merge values at sorted positions [0, index_to_merge) into SENTINEL_VALUE,
    apply Huffman coding, and return the effective average bits per symbol.

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

    list_dict = [SENTINEL_VALUE] + remaining_values
    list_freq = [merged_freq] + remaining_freqs

    _, codes, avg_length = huffman_encoding(list_dict, list_freq)

    sorted_codes = dict(
        sorted(codes.items(), key=lambda item: (len(item[1]), int(item[0])))
    )

    # Effective average bits per symbol:
    #   numerator   = Huffman bits for all real symbols (totalData)
    #               + Huffman bits for overhead table   (TABLE_OVERHEAD_SYMBOLS symbols)
    #               + ESCAPE_BITS * count of merged occurrences (list_freq[0])
    #   denominator = totalData   (we amortise overhead over real data)
    total_avg = (
        (avg_length * (total_data + TABLE_OVERHEAD_SYMBOLS))
        + (ESCAPE_BITS * list_freq[0])
    ) / total_data

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
        "merged_freq_sum": merged_freq,
        "merged_values": sorted_values[:index_to_merge].tolist(),
        "huffman_codes": sorted_codes,
        "avg_code_length": avg_length,
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
    if isinstance(obj, np.ndarray):
        return obj.tolist()
    if isinstance(obj, np.integer):
        return int(obj)
    if isinstance(obj, np.floating):
        return float(obj)
    if isinstance(obj, dict):
        return {k: _to_python(v) for k, v in obj.items()}
    if isinstance(obj, list):
        return [_to_python(v) for v in obj]
    return obj


def main():
    global g_debug

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
        default="./huffman_results",
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

    # Load data
    df = pd.read_csv(args.file)
    columns = df.columns.tolist()
    data = df.to_numpy()
    n_cols = df.shape[1]

    print(f"Loaded : {args.file}  shape={data.shape}")
    print(f"Columns: {columns}")

    value_dict = data[:, 0]
    print(
        f"\nValue column : '{columns[0]}'  shape={value_dict.shape}  dtype={value_dict.dtype}"
    )

    # Create output directory
    timestamp = datetime.now().strftime("%Y%m%d")
    args.output = os.path.join(args.output, timestamp)
    args.output = os.path.join(args.output, "norm")
    os.makedirs(args.output, exist_ok=True)

    # Process each frequency column
    for col_idx in range(1, n_cols):
        col_name = columns[col_idx]
        freq = data[:, col_idx]
        total = int(np.sum(freq))

        print(f"\n{'='*60}")
        print(f"Column '{col_name}'  |  total samples = {total:,}")
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
            json.dump(result_json, fh, indent=2)

        print(f"  Saved → {out_path}")


if __name__ == "__main__":
    main()

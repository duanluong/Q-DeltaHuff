"""
Sequential Model Difference and Frequency Calculator
Calculates differences between consecutive models and their frequency distributions
Includes all values from -255 to 255, even if frequency is 0
"""

import re
import numpy as np
import pandas as pd
import argparse
import sys
import glob
from pathlib import Path
from typing import Tuple, List
from datetime import datetime


def parse_c_header(file_path: str) -> Tuple[str, np.ndarray]:
    """
    Parse C header file and extract array name and data

    Args:
        file_path: Path to the C header file

    Returns:
        Tuple of (array_name, data_array)
    """
    with open(file_path, "r") as f:
        content = f.read()

    # Find array declaration
    array_pattern = r"unsigned\s+char\s+([a-zA-Z0-9_]+)\s*\[\]\s*=\s*\{"
    match = re.search(array_pattern, content)

    if not match:
        raise ValueError(f"Could not find array declaration in {file_path}")

    array_name = match.group(1)

    # Extract hex values
    hex_pattern = r"0x([0-9a-fA-F]{2})"
    hex_values = re.findall(hex_pattern, content)

    if not hex_values:
        raise ValueError(f"Could not find hex values in {file_path}")

    # Convert to numpy array
    data = np.array([int(h, 16) for h in hex_values], dtype=np.uint8)

    return array_name, data


def find_model_files(folder: str, model_name: str) -> List[Tuple[str, int]]:
    """
    Find all model files matching {model_name}_{i}.h pattern

    Args:
        folder: Directory containing model files
        model_name: Base model name

    Returns:
        List of (file_path, index) tuples sorted by index
    """
    pattern = f"{model_name}_*.h"
    search_path = str(Path(folder) / pattern)

    model_files = []
    for file_path in glob.glob(search_path):
        basename = Path(file_path).stem
        # Extract index from filename
        match = re.match(rf"{re.escape(model_name)}_(\d+)", basename)
        if match:
            index = int(match.group(1))
            model_files.append((file_path, index))

    # Sort by index
    model_files.sort(key=lambda x: x[1])

    return model_files


def calculate_difference(data1: np.ndarray, data2: np.ndarray) -> np.ndarray:
    """
    Calculate element-wise difference: data2 - data1

    Args:
        data1: First model data
        data2: Second model data

    Returns:
        Difference array (signed integers)
    """
    if len(data1) != len(data2):
        raise ValueError(f"Arrays have different sizes: {len(data1)} vs {len(data2)}")

    # Calculate difference as signed integers
    diff = data2.astype(np.int16) - data1.astype(np.int16)

    return diff


def calculate_full_frequency(diff: np.ndarray) -> pd.DataFrame:
    """
    Calculate frequency for ALL values from -255 to 255
    Values not present in diff will have frequency = 0

    Args:
        diff: Difference array

    Returns:
        DataFrame with columns [value, frequency]
    """
    # Create complete range from -255 to 255
    all_values = np.arange(-255, 256, dtype=np.int16)

    # Count frequencies in the difference array
    unique_vals, counts = np.unique(diff, return_counts=True)
    freq_dict = dict(zip(unique_vals, counts))

    # Create frequency array with 0 for missing values
    frequencies = np.array(
        [freq_dict.get(val, 0) for val in all_values], dtype=np.int64
    )

    # Create DataFrame
    freq_df = pd.DataFrame({"value": all_values, "frequency": frequencies})

    return freq_df


def combine_all_frequencies(folder: str, model_name: str, step: int, output_path: str):
    """
    Combine all individual frequency CSV files into a single combined CSV

    Args:
        folder: Directory containing individual CSV files
        model_name: Base model name (used to find CSV files)
        step: Step size used for processing
        output_path: Path to save combined CSV file

    Returns:
        Combined DataFrame
    """
    print("\n" + "=" * 70)
    print("COMBINING ALL FREQUENCY FILES")
    print("=" * 70)

    # Find all diff_and_freq CSV files
    pattern = f"diff_and_freq_*_*.csv"
    search_path = str(Path(folder) / pattern)
    csv_files = sorted(glob.glob(search_path))

    if not csv_files:
        print(f"No frequency CSV files found in {folder}")
        return None

    print(f"Found {len(csv_files)} frequency files")

    # Start with value column (-255 to 255)
    all_values = np.arange(-255, 256, dtype=np.int16)
    combined_data = {"value": all_values}

    # Read each CSV and add as a column
    for csv_file in csv_files:
        # Extract indices from filename: diff_and_freq_i_j.csv
        basename = Path(csv_file).stem
        match = re.search(r"diff_and_freq_(\d+)_(\d+)", basename)

        if match:
            idx1 = int(match.group(1))
            idx2 = int(match.group(2))

            # Read frequency data
            df = pd.read_csv(csv_file)

            # Create column name
            col_name = f"freq_{idx1}_{idx2}"

            # Add to combined data
            combined_data[col_name] = df["frequency"].values

            print(f"  Added: {col_name} from {Path(csv_file).name}")

    # Create combined DataFrame
    combined_df = pd.DataFrame(combined_data)

    # Save to CSV
    combined_df.to_csv(output_path, index=False)

    print(f"\n✓ Combined CSV saved to: {output_path}")
    print(f"  Dimensions: {combined_df.shape[0]} rows × {combined_df.shape[1]} columns")
    print(
        f"  Columns: value, {', '.join([c for c in combined_df.columns if c != 'value'])}"
    )
    print("=" * 70 + "\n")

    return combined_df


def single_pair_processing(file1: str, file2: str) -> pd.DataFrame:
    """
    Process a single pair of models and save frequency CSV

    Args:
        file1: Path to first model header
        file2: Path to second model header
    Returns:
        DataFrame with frequency distribution for the pair
    """
    # Parse both models
    _, data1 = parse_c_header(file1)
    _, data2 = parse_c_header(file2)

    print(f"  Model sizes: {len(data1)} bytes")

    # Calculate difference
    diff = calculate_difference(data1, data2)

    # Calculate full frequency distribution
    freq_df = calculate_full_frequency(diff)

    return freq_df


def processing(
    folder: str,
    model_name: str,
    output_dir: str = None,
    step: int = 1,
    combine: bool = False,
):
    """
    Process model pairs with specified step size and calculate differences

    Args:
        folder: Directory containing model files
        model_name: Base model name
        output_dir: Output directory for CSV files (default: same as folder)
        step: Step size for pairing models (default: 1 for consecutive pairs)
              step=1: compare (1,2), (2,3), (3,4), ...
              step=2: compare (1,3), (2,4), (3,5), ...
              step=5: compare (1,6), (2,7), (3,8), ...
        combine: Whether to combine all frequencies into a single CSV file

    Returns:
        Tuple of (success_count, failed_count, all_freq_dfs)
    """
    # Set output directory
    if output_dir is None:
        output_dir = folder
    else:
        today_str = datetime.now().strftime("%Y%m%d")
        output_dir = Path(output_dir) / today_str

    # Create output directory
    Path(output_dir).mkdir(exist_ok=True, parents=True)

    print("=" * 70)
    print("SEQUENTIAL MODEL DIFFERENCE AND FREQUENCY CALCULATOR")
    print("=" * 70)
    print(f"Folder:       {folder}")
    print(f"Model name:   {model_name}")
    print(f"Output dir:   {output_dir}")
    print(f"Step size:    {step} (compare model i with model i+{step})")
    print(f"Combine:      {combine}")
    print("=" * 70 + "\n")

    # Find all model files
    model_files = find_model_files(folder, model_name)

    if len(model_files) < 2:
        print(f"Error: Need at least 2 models, found {len(model_files)}")
        return 0, 0, []

    if len(model_files) <= step:
        print(f"Error: Step size ({step}) is too large for {len(model_files)} models")
        print(f"       Need at least {step + 1} models for step size {step}")
        return 0, 0, []

    print(f"Found {len(model_files)} models:")
    for file_path, idx in model_files:
        print(f"  [{idx}] {Path(file_path).name}")
    print()

    # Calculate number of pairs with given step
    total_pairs = len(model_files) - step
    print(f"Will create {total_pairs} pairs with step size {step}:")
    for i in range(total_pairs):
        idx1 = model_files[i][1]
        idx2 = model_files[i + step][1]
        print(f"  Pair {i+1}: Model {idx1} vs Model {idx2}")
    print()

    # Process pairs with step
    success_count = 0
    failed_count = 0
    all_freq_dfs = {}

    for i in range(len(model_files) - step):
        file1, idx1 = model_files[i]
        file2, idx2 = model_files[i + step]

        print(f"[{i+1}/{total_pairs}] Processing: Model {idx1} vs Model {idx2}")
        print(f"  File 1: {Path(file1).name}")
        print(f"  File 2: {Path(file2).name}")

        try:
            result = single_pair_processing(file1, file2)
            output_file = Path(output_dir) / f"diff_and_freq_{idx1}_{idx2}.csv"
            result.to_csv(output_file, index=False)
            all_freq_dfs[f"{idx1}_{idx2}"] = result
            success_count += 1
        except Exception as e:
            print(f"  ✗ Failed: {e}")
            failed_count += 1

        print()

    # Combine all frequencies if requested
    if combine and success_count > 0:
        combined_output = Path(output_dir) / f"{model_name}_diff_freq_step_{step}.csv"
        combine_all_frequencies(output_dir, model_name, step, str(combined_output))

    # Summary
    print("=" * 70)
    print("SUMMARY")
    print("=" * 70)
    print(f"Total pairs processed: {total_pairs}")
    print(f"Successful:            {success_count}")
    print(f"Failed:                {failed_count}")
    print(f"Output directory:      {output_dir}")

    if combine and success_count > 0:
        print(f"Combined file created: {model_name}_diff_freq_step_{step}.csv")

    print("=" * 70 + "\n")

    return success_count, failed_count, all_freq_dfs


def print_help_examples():
    """Print detailed usage examples"""
    help_text = """
╔══════════════════════════════════════════════════════════════════════════╗
║                      DETAILED USAGE EXAMPLES                             ║
╚══════════════════════════════════════════════════════════════════════════╝

1. BASIC USAGE - Consecutive pairs (default step=1)
   ------------------------------------------------
   python sequential_model_diff.py -f ./models -n LeNet_300_100
   
   With models: 1, 2, 3, 4, 5
   Creates pairs: (1,2), (2,3), (3,4), (4,5)
   Output files: diff_and_freq_1_2.csv
                 diff_and_freq_2_3.csv
                 diff_and_freq_3_4.csv
                 diff_and_freq_4_5.csv

2. CUSTOM STEP SIZE - Compare every 2nd model (step=2)
   ---------------------------------------------------
   python sequential_model_diff.py -f ./models -n LeNet_300_100 --step 2
   
   With models: 1, 2, 3, 4, 5, 6
   Creates pairs: (1,3), (2,4), (3,5), (4,6)
   Output files: diff_and_freq_1_3.csv
                 diff_and_freq_2_4.csv
                 diff_and_freq_3_5.csv
                 diff_and_freq_4_6.csv

3. LARGE STEP - Compare every 5th model (step=5)
   ----------------------------------------------
   python sequential_model_diff.py -f ./models -n LeNet_300_100 -s 5
   
   With models: 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
   Creates pairs: (1,6), (2,7), (3,8), (4,9), (5,10)
   Output files: diff_and_freq_1_6.csv
                 diff_and_freq_2_7.csv
                 diff_and_freq_3_8.csv
                 diff_and_freq_4_9.csv
                 diff_and_freq_5_10.csv

4. CUSTOM OUTPUT DIRECTORY
   ------------------------
   python sequential_model_diff.py -f ./models -n LeNet_300_100 -o ./results

5. LIST AVAILABLE MODELS (without processing)
   -------------------------------------------
   python sequential_model_diff.py -f ./models -n LeNet_300_100 --list
   
   This will show all matching models without processing them.

6. COMPLETE EXAMPLE WITH ALL OPTIONS
   ----------------------------------
   python sequential_model_diff.py \
       --folder ./models \
       --name LeNet_300_100 \
       --step 3 \
       --output ./frequency_results

╔══════════════════════════════════════════════════════════════════════════╗
║                         UNDERSTANDING STEP SIZE                          ║
╚══════════════════════════════════════════════════════════════════════════╝

Step size determines which models to compare:

• step=1 (default): Compare each model with the next one
  Models [1, 2, 3, 4, 5] → Pairs: (1,2), (2,3), (3,4), (4,5)
  Use case: Track gradual changes between consecutive training epochs

• step=2: Skip one model in between
  Models [1, 2, 3, 4, 5, 6] → Pairs: (1,3), (2,4), (3,5), (4,6)
  Use case: Compare alternate checkpoints

• step=5: Compare models 5 steps apart
  Models [1, 2, 3, 4, 5, 6, 7, 8, 9, 10] → Pairs: (1,6), (2,7), (3,8), (4,9), (5,10)
  Use case: Compare models from different training phases

• step=10: Large gap comparison
  Use case: Compare early vs late training stages

╔══════════════════════════════════════════════════════════════════════════╗
║                            OUTPUT FORMAT                                 ║
╚══════════════════════════════════════════════════════════════════════════╝

Each CSV file contains:
  value,frequency
  -255,0
  -254,3
  ...
  0,45000
  1,8234
  ...
  255,0

• 'value' column: All integers from -255 to 255 (511 total)
• 'frequency' column: Count of occurrences (0 if not present)
• Formula: diff[j] = model_{i+step}[j] - model_i[j]

"""
    print(help_text)


def list_available_models(folder: str, model_name: str):
    """
    List all available models matching the pattern

    Args:
        folder: Directory containing model files
        model_name: Base model name
    """
    print("=" * 70)
    print("AVAILABLE MODELS")
    print("=" * 70)
    print(f"Folder:     {folder}")
    print(f"Pattern:    {model_name}_*.h")
    print("=" * 70 + "\n")

    try:
        model_files = find_model_files(folder, model_name)

        if not model_files:
            print(f"No models found matching pattern: {model_name}_*.h")
            return

        print(f"Found {len(model_files)} models:\n")

        for i, (file_path, idx) in enumerate(model_files, 1):
            file_size = Path(file_path).stat().st_size
            print(f"  {i:2d}. Index {idx:3d}: {Path(file_path).name}")
            print(f"      Path: {file_path}")
            print(f"      Size: {file_size:,} bytes ({file_size/1024:.2f} KB)")
            print()

        print("=" * 70)
        print(f"Total: {len(model_files)} models")
        print(f"Index range: {model_files[0][1]} to {model_files[-1][1]}")
        print("=" * 70 + "\n")

        # Show what pairs would be created with different steps
        print("Possible comparisons with different step sizes:")
        print("-" * 70)

        for step in [1, 2, 5, 10]:
            if len(model_files) > step:
                num_pairs = len(model_files) - step
                print(f"\n  Step {step:2d}: {num_pairs} pairs")
                if num_pairs <= 5:
                    # Show all pairs
                    for i in range(num_pairs):
                        idx1 = model_files[i][1]
                        idx2 = model_files[i + step][1]
                        print(f"           ({idx1},{idx2})", end="")
                        if (i + 1) % 5 == 0:
                            print()
                    print()
                else:
                    # Show first few pairs
                    pairs = []
                    for i in range(min(3, num_pairs)):
                        idx1 = model_files[i][1]
                        idx2 = model_files[i + step][1]
                        pairs.append(f"({idx1},{idx2})")
                    print(
                        f"           {', '.join(pairs)}, ... (and {num_pairs - 3} more)"
                    )

        print("\n" + "=" * 70 + "\n")

    except Exception as e:
        print(f"Error: {e}")


def main():
    parser = argparse.ArgumentParser(
        description="Calculate sequential model differences and frequency distributions",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Process consecutive pairs (default)
  python sequential_model_diff.py -f ./models -n LeNet_300_100
  
  # Compare every 2nd model
  python sequential_model_diff.py -f ./models -n LeNet_300_100 --step 2
  
  # Compare every 5th model with custom output
  python sequential_model_diff.py -f ./models -n LeNet_300_100 -s 5 -o ./results
  
  # Combine all results into single CSV
  python sequential_model_diff.py -f ./models -n LeNet_300_100 --combine
  
  # Process with step and combine
  python sequential_model_diff.py -f ./models -n LeNet_300_100 -s 2 --combine
  
  # List available models without processing
  python sequential_model_diff.py -f ./models -n LeNet_300_100 --list
  
  # Show detailed examples
  python sequential_model_diff.py --help-examples

For more examples, use: --help-examples
        """,
    )

    parser.add_argument(
        "--folder", "-F", required=True, help="Folder containing model .h files"
    )
    parser.add_argument(
        "--name", "-n", required=True, help='Base model name (e.g., "LeNet-300-100")'
    )
    parser.add_argument(
        "--output",
        "-o",
        default=None,
        help="Output directory (default: same as input folder)",
    )
    parser.add_argument(
        "--step",
        "-s",
        type=int,
        default=1,
        help="Step size for model pairing (default: 1). "
        "step=1: (1,2), (2,3), (3,4); "
        "step=2: (1,3), (2,4), (3,5); "
        "step=5: (1,6), (2,7), (3,8)",
    )
    parser.add_argument(
        "--combine",
        "-c",
        action="store_true",
        help="Combine all frequency results into a single CSV file",
    )
    parser.add_argument(
        "--list",
        "-l",
        action="store_true",
        help="List available models and exit (no processing)",
    )
    parser.add_argument(
        "--help-examples", action="store_true", help="Show detailed examples and exit"
    )

    args = parser.parse_args()

    # Show detailed examples
    if args.help_examples:
        print_help_examples()
        sys.exit(0)

    # Validate folder
    if not Path(args.folder).exists():
        print(f"Error: Folder not found: {args.folder}", file=sys.stderr)
        sys.exit(1)

    # List models only
    if args.list:
        list_available_models(args.folder, args.name)
        sys.exit(0)

    # Validate step size
    if args.step < 1:
        print(f"Error: Step size must be at least 1, got {args.step}", file=sys.stderr)
        sys.exit(1)

    try:
        success, failed, _ = processing(
            args.folder, args.name, args.output, args.step, args.combine
        )

        # Exit with error if any failures
        sys.exit(0 if failed == 0 else 1)

    except Exception as e:
        print(f"\nError: {e}", file=sys.stderr)
        import traceback

        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()

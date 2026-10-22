#!/usr/bin/env python3
"""Fail if the firmware image takes more than a set share of its OTA slot (ADR-0004).

The slot size is read from the partition table, so a partition change can't silently leave
this check behind. Standard library only, so CI can run it without installing anything.

Usage: check_image_size.py IMAGE PARTITIONS_CSV [--slot app0] [--max-percent 90]
Writes a Markdown line to $GITHUB_STEP_SUMMARY when that is set.
"""

import argparse
import os
import sys
from pathlib import Path


def slot_size(partitions_csv: Path, name: str) -> int:
    for raw in partitions_csv.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [f.strip() for f in line.split(",")]
        if fields[0] == name:
            return int(fields[4], 0)  # accepts 0x600000 or 6291456
    raise SystemExit(f"error: partition '{name}' not found in {partitions_csv}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("image", type=Path)
    parser.add_argument("partitions", type=Path)
    parser.add_argument("--slot", default="app0")
    parser.add_argument("--max-percent", type=float, default=90.0)
    args = parser.parse_args()

    image = args.image.stat().st_size
    slot = slot_size(args.partitions, args.slot)
    limit = int(slot * args.max_percent / 100)
    percent = 100 * image / slot
    ok = image <= limit

    message = (
        f"{args.image.name}: {image:,} B = {percent:.1f} % of the {slot:,} B '{args.slot}' slot "
        f"(limit {args.max_percent:g} % = {limit:,} B)"
    )
    print(("OK: " if ok else "FAIL: ") + message)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write(f"**Image size** {'✅' if ok else '❌'} {message}\n")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""
Build or compose a unified S5L8702 NAND image for use as a QEMU
BlockBackend-backed image (see hw/misc/s5l8702-nand.c / .h).

Layout must match whatever geometry the device is compiled/configured with
(NAND_SPARE_STRIDE, NAND_BANK_CAPACITY, NAND_BYTES_PER_PAGE,
NAND_NUM_BANKS_INSTALLED in include/hw/misc/s5l8702-nand.h): banks and
their spare bytes are interwoven, one page's data immediately followed by
its spare bytes, banks concatenated back to back.

    for bank in 0 .. num-banks-1:
      for page in 0 .. pages-per-bank-1:
        [ page-size bytes: page data ][ spare-stride bytes: spare ]

Geometry defaults below match this fork's current compile-time NAND
geometry; override with --page-size/--spare-stride/--bank-capacity/
--num-banks if the device is built with something different.

Usage:
    # Fast path: a blank image, every byte 0xFF.
    ./nand-interleave.py --blank -o nand-base.raw

    # Compose from existing flat per-bank data/spare files (e.g. real dumps,
    # or the split files this fork used before the unified image).
    ./nand-interleave.py \\
        --bank bank0.bin --spare spare0.bin \\
        --bank bank1.bin --spare spare1.bin \\
        -o nand-base.raw

    # Non-default geometry (e.g. 8 KiB pages, 32-byte spare, 4 GiB banks).
    ./nand-interleave.py --blank -o nand-base.raw \\
        --page-size 8192 --spare-stride 32 --bank-capacity 4G --num-banks 4

Then layer a QCOW2 overlay the standard way:
    qemu-img create -f qcow2 -b nand-base.raw -F raw nand-overlay.qcow2
"""

import argparse
import re
import sys

# Defaults match current compile-time geometry (include/hw/misc/s5l8702-nand.h).
# Override on the command line for other geometries.
DEFAULT_PAGE_SIZE = 2048
DEFAULT_SPARE_STRIDE = 16
DEFAULT_BANK_CAPACITY = 2 * 1024 * 1024 * 1024  # page-data bytes per bank
DEFAULT_NUM_BANKS = 2

CHUNK_PAGES = 4096  # write in batches to keep memory use bounded

_SIZE_SUFFIXES = {"": 1, "K": 1024, "M": 1024**2, "G": 1024**3, "T": 1024**4}


def parse_size(value):
    """Parse a byte count, accepting optional K/M/G/T (and KiB/MiB/... spellings)."""
    m = re.fullmatch(r"\s*(\d+)\s*([KMGT]?)(i?B)?\s*", value, re.IGNORECASE)
    if not m:
        raise argparse.ArgumentTypeError(f"invalid size: {value!r} (expected e.g. 2048, 16M, 2GiB)")
    number, suffix = m.group(1), m.group(2).upper()
    return int(number) * _SIZE_SUFFIXES[suffix]


class Geometry:
    def __init__(self, page_size, spare_stride, bank_capacity):
        self.page_size = page_size
        self.spare_stride = spare_stride
        self.bank_capacity = bank_capacity
        self.pages_per_bank = bank_capacity // page_size
        self.record_size = page_size + spare_stride
        self.bank_stride = self.pages_per_bank * self.record_size


def write_blank(out_path, num_banks, geo):
    ff_record = b"\xff" * geo.record_size
    chunk = ff_record * CHUNK_PAGES
    with open(out_path, "wb") as out:
        for _bank in range(num_banks):
            remaining = geo.pages_per_bank
            while remaining > 0:
                n = min(CHUNK_PAGES, remaining)
                out.write(chunk if n == CHUNK_PAGES else ff_record * n)
                remaining -= n


def write_interleaved(out_path, bank_spare_pairs, geo):
    with open(out_path, "wb") as out:
        for bank_path, spare_path in bank_spare_pairs:
            with open(bank_path, "rb") as bank_f, open(spare_path, "rb") as spare_f:
                for _page in range(geo.pages_per_bank):
                    data = bank_f.read(geo.page_size)
                    if len(data) < geo.page_size:
                        data = data + b"\xff" * (geo.page_size - len(data))
                    spare = spare_f.read(geo.spare_stride)
                    if len(spare) < geo.spare_stride:
                        spare = spare + b"\xff" * (geo.spare_stride - len(spare))
                    out.write(data)
                    out.write(spare)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-o", "--output", required=True, help="output image path")
    parser.add_argument("--blank", action="store_true", help="write an all-0xFF image instead of composing from files")
    parser.add_argument("--num-banks", type=int, default=DEFAULT_NUM_BANKS,
                         help=f"number of banks to write in --blank mode (default: {DEFAULT_NUM_BANKS}, "
                              "matches NAND_NUM_BANKS_INSTALLED)")
    parser.add_argument("--bank", action="append", default=[], help="a flat per-bank data file (repeat, paired in order with --spare)")
    parser.add_argument("--spare", action="append", default=[], help="a flat per-bank spare file (repeat, paired in order with --bank)")
    parser.add_argument("--page-size", type=parse_size, default=DEFAULT_PAGE_SIZE,
                         help=f"bytes of page data per page (default: {DEFAULT_PAGE_SIZE}, matches NAND_BYTES_PER_PAGE)")
    parser.add_argument("--spare-stride", type=parse_size, default=DEFAULT_SPARE_STRIDE,
                         help=f"bytes of spare/OOB data per page (default: {DEFAULT_SPARE_STRIDE}, matches NAND_SPARE_STRIDE)")
    parser.add_argument("--bank-capacity", type=parse_size, default=DEFAULT_BANK_CAPACITY,
                         help=f"page-data bytes per bank, accepts K/M/G/T suffixes (default: {DEFAULT_BANK_CAPACITY}, "
                              "matches NAND_BANK_CAPACITY)")
    args = parser.parse_args()

    geo = Geometry(args.page_size, args.spare_stride, args.bank_capacity)

    if args.blank:
        if args.bank or args.spare:
            parser.error("--blank cannot be combined with --bank/--spare")
        print(f"writing blank image: {args.num_banks} bank(s), {geo.bank_stride} bytes/bank, "
              f"{args.num_banks * geo.bank_stride} bytes total "
              f"(page-size={geo.page_size} spare-stride={geo.spare_stride} bank-capacity={geo.bank_capacity})")
        write_blank(args.output, args.num_banks, geo)
    else:
        if len(args.bank) != len(args.spare) or not args.bank:
            parser.error("--bank and --spare must be given the same number of times, at least once (or use --blank)")
        print(f"writing interleaved image: {len(args.bank)} bank(s), {geo.bank_stride} bytes/bank, "
              f"{len(args.bank) * geo.bank_stride} bytes total "
              f"(page-size={geo.page_size} spare-stride={geo.spare_stride} bank-capacity={geo.bank_capacity})")
        write_interleaved(args.output, list(zip(args.bank, args.spare)), geo)

    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

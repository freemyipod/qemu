#!/usr/bin/env python3
"""
Create and manage self-describing S5L8702 NAND qcow2 images.

The emulated NAND controller (hw/misc/s5l8702-nand.c) reads its geometry
from a custom qcow2 header extension (magic 0x4E414E44, "NAND") instead of
compile-time constants, so the image carries its own layout:

    struct Qcow2NandGeometry {        /* all fields big-endian */
        uint32_t version;             /* 2 */
        uint32_t page_size;           /* data bytes per page */
        uint32_t spare_stride;        /* spare bytes stored per page */
        uint32_t pages_per_block;     /* pages erased together */
        uint32_t num_banks;           /* installed banks */
        uint64_t bank_capacity;       /* page-data bytes per bank */
        uint32_t nand_id;             /* chip ID reported by NAND_CMD_ID */
    };

The image body is the interleaved layout the device expects: banks back to
back, each page's data immediately followed by its spare bytes.

Subcommands:
    create   Blank NAND image, every byte 0xFF (compressed clusters by
             default, so an "empty" 16 GiB NAND is only a few MiB on disk):
                 ./nand-image.py create nand-base.qcow2
                 ./nand-image.py create nand-base.qcow2 \\
                     --page-size 2048 --spare-stride 16 \\
                     --pages-per-block 128 --banks 2 --bank-capacity 8G \\
                     --nand-id 0xA5D5D589

    overlay  Sparse COW overlay backed by an existing image (geometry is
             copied from the backing image's extension when present):
                 ./nand-image.py overlay nand.qcow2 -b nand-base.qcow2

    stamp    Add/replace the geometry extension on an existing qcow2
             (migration path for images made before this scheme):
                 ./nand-image.py stamp old-nand.qcow2 --bank-capacity 8G

    info     Show the geometry extension (follows the backing chain):
                 ./nand-image.py info nand.qcow2

`--nand-id` (create/overlay/stamp) sets the chip ID reported by the
NAND_CMD_ID command, decimal or 0x-hex (default 0xA5D5D589, matching
NAND_CHIP_ID in include/hw/misc/s5l8702-nand.h). `overlay` inherits it from
the backing image's extension unless any geometry flag, including
--nand-id, is passed explicitly.

Then run QEMU with:
    -drive if=mtd,index=1,format=qcow2,file=nand.qcow2

`create` and `overlay` shell out to qemu-img/qemu-io (found in ./build next
to this script, or on $PATH; override with --qemu-bin-dir). `stamp` and
`info` are pure Python.
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys

NAND_EXT_MAGIC = 0x4E414E44  # "NAND"
NAND_EXT_VERSION = 2
NAND_EXT_VERSION_1_FMT = ">IIIIIQ"  # legacy: no nand_id
# version, page_size, spare_stride, pages_per_block, num_banks, bank_capacity, nand_id
NAND_EXT_FMT = ">IIIIIQI"

QCOW2_MAGIC = 0x514649FB  # "QFI\xfb"

# Defaults match the geometry this fork historically compiled in
# (include/hw/misc/s5l8702-nand.h before it went runtime).
DEFAULT_PAGE_SIZE = 2048
DEFAULT_SPARE_STRIDE = 16
DEFAULT_PAGES_PER_BLOCK = 128
DEFAULT_NUM_BANKS = 2
DEFAULT_BANK_CAPACITY = 2 * 1024 * 1024 * 1024
# Matches NAND_CHIP_ID in include/hw/misc/s5l8702-nand.h.
DEFAULT_NAND_ID = 0xA5D5D589

FMI_SECTOR_SIZE = 0x800  # fixed FMI DMA/ECC sector (see s5l8702-nand.h)

FILL_CHUNK = 128 * 1024 * 1024  # bytes per qemu-io write command

_SIZE_SUFFIXES = {"": 1, "K": 1024, "M": 1024**2, "G": 1024**3, "T": 1024**4}


def human_size(n):
    for unit in ("B", "KiB", "MiB", "GiB", "TiB"):
        if n < 1024 or unit == "TiB":
            return f"{n:g} {unit}" if n == int(n) else f"{n:.1f} {unit}"
        n /= 1024


def parse_size(value):
    """Parse a byte count, accepting optional K/M/G/T (and KiB/MiB/... spellings)."""
    m = re.fullmatch(r"\s*(\d+)\s*([KMGT]?)(i?B)?\s*", value, re.IGNORECASE)
    if not m:
        raise argparse.ArgumentTypeError(f"invalid size: {value!r} (expected e.g. 2048, 16M, 8GiB)")
    number, suffix = m.group(1), m.group(2).upper()
    return int(number) * _SIZE_SUFFIXES[suffix]


def parse_nand_id(value):
    """Parse a 32-bit NAND chip ID, accepting decimal or 0x-prefixed hex."""
    try:
        n = int(value, 0)
    except ValueError:
        raise argparse.ArgumentTypeError(f"invalid NAND ID: {value!r} (expected e.g. 0xA5D5D589)")
    if not 0 <= n <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError(f"NAND ID {value!r} out of range for a 32-bit value")
    return n


class Geometry:
    def __init__(self, page_size, spare_stride, pages_per_block, num_banks, bank_capacity,
                nand_id=DEFAULT_NAND_ID):
        self.page_size = page_size
        self.spare_stride = spare_stride
        self.pages_per_block = pages_per_block
        self.num_banks = num_banks
        self.bank_capacity = bank_capacity
        self.nand_id = nand_id

    def validate(self):
        if self.page_size == 0 or self.page_size % FMI_SECTOR_SIZE:
            raise SystemExit(f"error: page size {self.page_size} is not a multiple of "
                             f"the {FMI_SECTOR_SIZE}-byte FMI sector")
        if self.spare_stride < 12:
            raise SystemExit(f"error: spare stride {self.spare_stride} too small "
                             "(the controller stores 12 metadata bytes per page)")
        if not self.pages_per_block:
            raise SystemExit("error: pages-per-block must be non-zero")
        if not 1 <= self.num_banks <= 8:
            raise SystemExit(f"error: bank count {self.num_banks} out of range (1..8)")
        if self.bank_capacity == 0 or self.bank_capacity % self.page_size:
            raise SystemExit(f"error: bank capacity {self.bank_capacity} is not a "
                             f"multiple of the page size {self.page_size}")

    @property
    def pages_per_bank(self):
        return self.bank_capacity // self.page_size

    @property
    def page_record_size(self):
        return self.page_size + self.spare_stride

    @property
    def total_size(self):
        return self.num_banks * self.pages_per_bank * self.page_record_size

    def pack(self):
        return struct.pack(NAND_EXT_FMT, NAND_EXT_VERSION, self.page_size,
                           self.spare_stride, self.pages_per_block,
                           self.num_banks, self.bank_capacity, self.nand_id)

    @classmethod
    def unpack(cls, data):
        if len(data) >= struct.calcsize(NAND_EXT_FMT):
            version, page_size, spare_stride, ppb, banks, cap, nand_id = \
                struct.unpack_from(NAND_EXT_FMT, data)
            if version == NAND_EXT_VERSION:
                return cls(page_size, spare_stride, ppb, banks, cap, nand_id)
        # Fall back to the legacy v1 layout (no nand_id field).
        if len(data) < struct.calcsize(NAND_EXT_VERSION_1_FMT):
            raise ValueError(f"geometry extension too short ({len(data)} bytes)")
        version, page_size, spare_stride, ppb, banks, cap = \
            struct.unpack_from(NAND_EXT_VERSION_1_FMT, data)
        if version != 1:
            raise ValueError(f"unsupported geometry extension version {version}")
        return cls(page_size, spare_stride, ppb, banks, cap, DEFAULT_NAND_ID)

    def describe(self):
        return (f"  page size:        {self.page_size}\n"
                f"  spare stride:     {self.spare_stride}\n"
                f"  pages per block:  {self.pages_per_block}\n"
                f"  banks installed:  {self.num_banks}\n"
                f"  bank capacity:    {self.bank_capacity} ({human_size(self.bank_capacity)})\n"
                f"  pages per bank:   {self.pages_per_bank}\n"
                f"  page record size: {self.page_record_size}\n"
                f"  nand id:          0x{self.nand_id:08X}\n"
                f"  image size:       {self.total_size} ({human_size(self.total_size)})")


# ---------------------------------------------------------------------------
# Minimal qcow2 header (de)serialization: just enough to add/inspect header
# extensions in place. Mirrors tests/qemu-iotests/qcow2_format.py.
# ---------------------------------------------------------------------------

V2_HEADER_SIZE = 72
V3_HEADER_FIELDS_SIZE = 104  # through header_length


class Qcow2Image:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.raw = f.read(8 * 1024 * 1024)  # header cluster(s); plenty

        if len(self.raw) < V2_HEADER_SIZE:
            raise SystemExit(f"error: {path}: too small to be a qcow2 image")
        (magic, self.version) = struct.unpack_from(">II", self.raw, 0)
        if magic != QCOW2_MAGIC:
            raise SystemExit(f"error: {path}: not a qcow2 image")
        if self.version not in (2, 3):
            raise SystemExit(f"error: {path}: unsupported qcow2 version {self.version}")

        (self.backing_file_offset, self.backing_file_size,
         self.cluster_bits, self.virtual_size) = struct.unpack_from(">QIIQ", self.raw, 8)

        if self.version == 2:
            self.header_length = V2_HEADER_SIZE
        else:
            (self.header_length,) = struct.unpack_from(">I", self.raw, 100)
            if self.header_length < V3_HEADER_FIELDS_SIZE:
                raise SystemExit(f"error: {path}: bogus header_length {self.header_length}")

        self.cluster_size = 1 << self.cluster_bits
        self.backing_file = None
        if self.backing_file_offset:
            self.backing_file = self.raw[self.backing_file_offset:
                                         self.backing_file_offset + self.backing_file_size]

        # Parse extensions: (magic, data) pairs, order preserved.
        self.extensions = []
        end = self.backing_file_offset or self.cluster_size
        off = self.header_length
        while off <= end - 8:
            ext_magic, ext_len = struct.unpack_from(">II", self.raw, off)
            if ext_magic == 0:
                break
            data = self.raw[off + 8:off + 8 + ext_len]
            if len(data) < ext_len:
                raise SystemExit(f"error: {path}: truncated header extension "
                                 f"0x{ext_magic:08x}")
            self.extensions.append((ext_magic, data))
            off += 8 + ((ext_len + 7) & ~7)

    def get_extension(self, magic):
        for ext_magic, data in self.extensions:
            if ext_magic == magic:
                return data
        return None

    def set_extension(self, magic, data):
        self.extensions = [(m, d) for m, d in self.extensions if m != magic]
        self.extensions.append((magic, data))

    def write_header(self):
        buf = bytearray(self.raw[:self.header_length])
        off = self.header_length
        for ext_magic, data in self.extensions:
            padded = (len(data) + 7) & ~7
            buf += struct.pack(">II", ext_magic, len(data))
            buf += data + b"\0" * (padded - len(data))
            off += 8 + padded
        buf += struct.pack(">II", 0, 0)  # end-of-extensions marker
        off += 8

        if self.backing_file:
            struct.pack_into(">QI", buf, 8, off, len(self.backing_file))
            buf += self.backing_file
            off += len(self.backing_file)

        if off > self.cluster_size:
            raise SystemExit(f"error: {self.path}: header extensions do not fit "
                             f"in the first cluster ({off} > {self.cluster_size})")
        buf += b"\0" * (self.cluster_size - len(buf) % self.cluster_size
                        if len(buf) % self.cluster_size else 0)

        with open(self.path, "r+b") as f:
            f.write(buf)


# ---------------------------------------------------------------------------
# qemu-img / qemu-io discovery and invocation
# ---------------------------------------------------------------------------

def find_qemu_bin(name, bin_dir):
    candidates = []
    if bin_dir:
        candidates.append(os.path.join(bin_dir, name))
    else:
        here = os.path.dirname(os.path.abspath(__file__))
        candidates.append(os.path.join(here, "build", name))
        found = shutil.which(name)
        if found:
            candidates.append(found)
    for c in candidates:
        if os.access(c, os.X_OK):
            return c
    raise SystemExit(f"error: cannot find {name} (tried {', '.join(candidates)}); "
                     "use --qemu-bin-dir")


def run(argv):
    proc = subprocess.run(argv)
    if proc.returncode:
        raise SystemExit(f"error: {' '.join(argv)} failed with status {proc.returncode}")


def fill_ff(qemu_io, path, total, cluster_size, compress):
    """Fill [0, total) of a qcow2 image with 0xFF via qemu-io."""
    argv = [qemu_io, "-f", "qcow2", path]
    nchunks = 0
    off = 0
    while off < total:
        length = min(FILL_CHUNK, total - off)
        flags = "-q"
        # Compressed writes must cover whole, aligned clusters; write any
        # unaligned tail uncompressed.
        if compress and length % cluster_size == 0:
            flags += " -c"
        argv += ["-c", f"write {flags} -P 0xff {off} {length}"]
        nchunks += 1
        off += length

    print(f"filling {human_size(total)} with 0xFF "
          f"({'compressed' if compress else 'fully allocated'}, "
          f"{nchunks} chunks)...")
    run(argv)


# ---------------------------------------------------------------------------
# Subcommands
# ---------------------------------------------------------------------------

def geometry_from_args(args):
    return Geometry(args.page_size, args.spare_stride, args.pages_per_block,
                    args.banks, args.bank_capacity, args.nand_id)


def stamp_image(path, geo):
    img = Qcow2Image(path)
    if img.virtual_size < geo.total_size:
        print(f"warning: image virtual size ({img.virtual_size}) is smaller than "
              f"the geometry requires ({geo.total_size}); the device will refuse it",
              file=sys.stderr)
    img.set_extension(NAND_EXT_MAGIC, geo.pack())
    img.write_header()


def cmd_create(args):
    geo = geometry_from_args(args)
    geo.validate()
    qemu_img = find_qemu_bin("qemu-img", args.qemu_bin_dir)
    qemu_io = find_qemu_bin("qemu-io", args.qemu_bin_dir)

    run([qemu_img, "create", "-q", "-f", "qcow2", args.image, str(geo.total_size)])
    img = Qcow2Image(args.image)
    fill_ff(qemu_io, args.image, geo.total_size, img.cluster_size,
            compress=not args.no_compress)
    stamp_image(args.image, geo)
    print(f"created {args.image}:")
    print(geo.describe())


def cmd_overlay(args):
    qemu_img = find_qemu_bin("qemu-img", args.qemu_bin_dir)

    backing_geo = None
    if args.backing_format != "raw":
        data = Qcow2Image(args.backing).get_extension(NAND_EXT_MAGIC)
        if data is not None:
            backing_geo = Geometry.unpack(data)

    if backing_geo is not None:
        geo = backing_geo
    elif args.explicit_geometry:
        geo = geometry_from_args(args)
    else:
        raise SystemExit("error: backing image has no geometry extension; "
                         "pass the geometry flags explicitly (or stamp the "
                         "backing image first)")
    geo.validate()

    run([qemu_img, "create", "-q", "-f", "qcow2",
         "-b", os.path.abspath(args.backing), "-F", args.backing_format,
         args.image, str(geo.total_size)])
    stamp_image(args.image, geo)
    print(f"created overlay {args.image} (backing: {args.backing}):")
    print(geo.describe())


def cmd_stamp(args):
    geo = geometry_from_args(args)
    geo.validate()
    stamp_image(args.image, geo)
    print(f"stamped {args.image}:")
    print(geo.describe())


def cmd_info(args):
    path = args.image
    seen = set()
    while True:
        img = Qcow2Image(path)
        data = img.get_extension(NAND_EXT_MAGIC)
        if data is not None:
            geo = Geometry.unpack(data)
            print(f"{path}: NAND geometry extension:")
            print(geo.describe())
            return
        print(f"{path}: no NAND geometry extension")
        if not img.backing_file:
            raise SystemExit(1)
        backing = img.backing_file.decode("utf-8", "replace")
        if not os.path.isabs(backing):
            backing = os.path.join(os.path.dirname(os.path.abspath(path)), backing)
        if backing in seen:
            raise SystemExit(f"error: backing chain loop at {backing}")
        seen.add(backing)
        print(f"  following backing file: {backing}")
        path = backing


def add_geometry_args(parser):
    parser.add_argument("--page-size", type=parse_size, default=DEFAULT_PAGE_SIZE,
                        help=f"data bytes per page (default {DEFAULT_PAGE_SIZE})")
    parser.add_argument("--spare-stride", type=parse_size, default=DEFAULT_SPARE_STRIDE,
                        help=f"spare bytes stored per page (default {DEFAULT_SPARE_STRIDE})")
    parser.add_argument("--pages-per-block", type=int, default=DEFAULT_PAGES_PER_BLOCK,
                        help=f"pages erased together (default {DEFAULT_PAGES_PER_BLOCK})")
    parser.add_argument("--banks", type=int, default=DEFAULT_NUM_BANKS,
                        help=f"installed banks (default {DEFAULT_NUM_BANKS})")
    parser.add_argument("--bank-capacity", type=parse_size, default=DEFAULT_BANK_CAPACITY,
                        help="page-data bytes per bank (default 8G)")
    parser.add_argument("--nand-id", type=parse_nand_id, default=DEFAULT_NAND_ID,
                        help="32-bit chip ID reported by the NAND_CMD_ID command, "
                             f"decimal or 0x-hex (default 0x{DEFAULT_NAND_ID:08X})")


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--qemu-bin-dir",
                        help="directory containing qemu-img/qemu-io "
                             "(default: ./build next to this script, then $PATH)")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("create", help="create a blank all-0xFF NAND image")
    p.add_argument("image")
    add_geometry_args(p)
    p.add_argument("--no-compress", action="store_true",
                   help="fully allocate instead of using compressed clusters")
    p.set_defaults(func=cmd_create)

    p = sub.add_parser("overlay", help="create a sparse overlay over a base image")
    p.add_argument("image")
    p.add_argument("-b", "--backing", required=True, help="backing image path")
    p.add_argument("-F", "--backing-format", default="qcow2",
                   choices=["qcow2", "raw"], help="backing format (default qcow2)")
    add_geometry_args(p)
    p.set_defaults(func=cmd_overlay)

    p = sub.add_parser("stamp", help="add/replace the geometry extension on an existing qcow2")
    p.add_argument("image")
    add_geometry_args(p)
    p.set_defaults(func=cmd_stamp)

    p = sub.add_parser("info", help="show the geometry extension of an image")
    p.add_argument("image")
    p.set_defaults(func=cmd_info)

    args = parser.parse_args()
    # Track whether the user explicitly supplied geometry (overlay needs to
    # distinguish "inherit from backing" from "user asked for this layout").
    args.explicit_geometry = any(
        a.startswith(("--page-size", "--spare-stride", "--pages-per-block",
                      "--banks", "--bank-capacity", "--nand-id")) for a in sys.argv[1:])
    args.func(args)


if __name__ == "__main__":
    main()

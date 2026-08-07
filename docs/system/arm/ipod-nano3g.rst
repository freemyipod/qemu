iPod Nano 3G (``ipod-nano3g``)
==============================

Emulates the Samsung S5L8702 SoC as found in the iPod Nano 3rd generation.
This page documents the NAND flash controller (``s5l8702-nand``), which is the
part of the machine with the most knobs.

Booting the machine
-------------------

The machine takes a boot ROM image as a machine property, a NOR flash image on
``if=mtd`` index 0, and a NAND image on ``if=mtd`` index 1. Index order is what
selects them, so pass the NOR drive first:

.. code-block:: console

   $ qemu-system-arm -M ipod-nano3g,bootrom=s5l8702_bootrom.bin \
       -drive if=mtd,format=raw,file=nor.bin \
       -drive if=mtd,format=qcow2,file=nand.qcow2

With no NAND drive the controller comes up in a stub mode with a built-in
geometry and no backing storage. That is enough to get through code that only
probes the chip, not to boot a firmware that mounts it.

NAND images
-----------

The controller does not hardcode a geometry. It reads one from a custom qcow2
header extension (magic ``0x4E414E44``, ``"NAND"``) so that an image describes
its own layout:

.. code-block:: c

   struct Qcow2NandGeometry {        /* all fields big-endian */
       uint32_t version;             /* 2 */
       uint32_t page_size;           /* data bytes per page */
       uint32_t spare_stride;        /* spare bytes stored per page */
       uint32_t pages_per_block;     /* pages erased together */
       uint32_t num_banks;           /* installed banks */
       uint64_t bank_capacity;       /* page-data bytes per bank */
       uint32_t nand_id;             /* chip ID reported by NAND_CMD_ID */
   };

The image body is the layout the device expects: banks back to back, each
page's data immediately followed by its spare bytes.

Images are made with ``nand-image.py`` from the ``nand-qcow`` project, which
writes that extension for you.

Creating a blank image
~~~~~~~~~~~~~~~~~~~~~~

``create`` produces an all-0xFF image. Clusters are compressed by default, so
a blank 16 GiB NAND costs only a few MiB on disk:

.. code-block:: console

   $ ./nand-image.py create nand-base.qcow2
   $ ./nand-image.py create nand-base.qcow2 \
       --page-size 2048 --spare-stride 16 \
       --pages-per-block 128 --banks 2 --bank-capacity 8G \
       --nand-id 0xA5D5D589

Always run the machine against a *throwaway overlay*, never against a base
image or a dump you care about - the firmware formats the chip on boot:

.. code-block:: console

   $ ./nand-image.py overlay nand.qcow2 -b nand-base.qcow2

``overlay`` copies the geometry from the backing image. ``info`` prints the
geometry (following the backing chain), and ``stamp`` adds or replaces the
extension on an image made before this scheme existed:

.. code-block:: console

   $ ./nand-image.py info nand.qcow2
   $ ./nand-image.py stamp old-nand.qcow2 --bank-capacity 8G

Device properties
-----------------

Set with ``-global s5l8702-nand.<property>=<value>``.

``raw-ecc-layout`` (bool, default ``off``)
  Interpret the image as a *physical* dump taken off a real chip rather than
  the logical layout this model writes.

  On the hardware the FMI stores a page as a series of BCH chunks, each
  ``[3B metadata stripe][13B parity][512B data]``, with the firmware's 12-byte
  metadata striped across the first four chunks - not as data followed by
  spare. With this on, reads de-interleave the record and writes re-interleave
  it, which is what makes a chip dump readable by the firmware.

  Off by default because images this model created are logical-layout, and
  flipping the interpretation under them would break every existing one. Turn
  it on for an image dumped straight off a chip. It requires a drive.

  Parity is *not* computed on write (there is no BCH engine here), so an image
  written in this mode is readable by QEMU but is **not** valid to flash back
  onto a chip.

``erase-pages`` (uint32, default ``0``)
  Pages cleared by one ERASE command - the physical block size. ``0`` uses the
  image header's ``pages_per_block``.

  These differ when the header records what the *firmware* treats as a block.
  For example, the 16GB Micron part's driver models the chip as 4096 × 256-page
  superblocks, while the silicon has 8192 × 128-page blocks in two planes
  (superblock ``i`` is physical blocks ``2i``/``2i+1``, plane selected by row
  bit 7). Erasing such a superblock takes *two* erase commands, so a model that
  cleared 256 pages per command would erase both planes at once and hide
  whether the firmware's two-plane erase handling works at all. For that image,
  pass ``erase-pages=128``.

``fmiss-enable`` (bool, default ``off``)
  Run the real FMISS micro-VM instead of the paravirtualised dispatcher. See
  `FMISS`_ below.

``fault-blocks`` / ``fault-ops`` / ``fault-bank``
  Fault injection. See `Injecting failures`_ below.

``drive``
  Set by the machine from ``if=mtd`` index 1; you do not normally set it
  directly.

FMISS
-----

Page reads, programs and erases are driven by small programs the firmware
uploads to the controller's FMISS engine. This model can handle them two ways.

By default (``fmiss-enable=off``) it *paravirtualises* them: it hashes the
program at ``FMI_PROGRAM`` and looks the hash up in a table of known programs,
running a handwritten C replacement for each. This is fast and reliable for
firmware whose programs are already in the table. An unrecognised program is
not executed - you get a warning naming its hash and address:

.. code-block:: none

   s5l8702-nand: unrecognized FMISS program at 0x%08x (hash=0x%016...);
   no paravirtualized handler for it yet

That warning is the signal to try the interpreter instead:

.. code-block:: console

   $ qemu-system-arm ... -global s5l8702-nand.fmiss-enable=on

``fmiss-enable=on`` runs the actual FMISS instruction set. It is
**experimental and incomplete**: it is confirmed working for reading IDs and
pages, but has not been confirmed for writing or erasing. Prefer the default
unless you are working with firmware the PV table does not cover.

Injecting failures
------------------

The model can fail program and erase operations on chosen blocks, so that
firmware error paths - a worn block, a factory-bad block the driver failed to
exclude - can be exercised deterministically.

``fault-blocks``
  Comma-separated block numbers, decimal or ``0x``-hex. Empty (the default)
  disables injection entirely.

``fault-ops``
  ``program``, ``erase`` or ``both`` (default ``both``).

``fault-bank``
  Restrict injection to one bank. ``-1`` (default) means all banks.

Example - fail every program aimed at block 1964, on every bank:

.. code-block:: console

   $ qemu-system-arm -M ipod-nano3g,bootrom=s5l8702_bootrom.bin \
       -drive if=mtd,format=raw,file=nor.bin \
       -drive if=mtd,format=qcow2,file=nand.qcow2 \
       -global s5l8702-nand.raw-ecc-layout=on \
       -global s5l8702-nand.erase-pages=128 \
       -global s5l8702-nand.fault-blocks=1964 \
       -global s5l8702-nand.fault-ops=program \
       -trace s5l8702_nand_fault_injected

A faulted operation latches FAIL in the status register and leaves the block's
contents alone, which is what a worn block does - the firmware must not see a
successfully erased or programmed block. Each hit is traced:

.. code-block:: none

   INJECTED FAULT: program failed on bank=0 block=1964

Injection is armed at realize time, and says so:

.. code-block:: none

   s5l8702-nand: fault injection armed: 1 block(s) [1964], ops=program, bank=all

.. note::

   Block numbers here are always ``row_address / pages_per_block`` using the
   *image header's* ``pages_per_block``, for both ops and regardless of
   ``erase-pages``. On an image whose header describes 256-page superblocks,
   ``fault-blocks`` therefore names superblocks. This is deliberate: it keeps
   the unit the firmware reasons in.

Simulating factory-bad blocks
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Fault injection models a block that *fails*, which is not the same as a block
the manufacturer *marked* bad. A factory bad-block marker is data, so it is
made by writing to the image rather than by a device property: put a non-0xFF
byte at the start of the spare area of pages 0 and 1 of the physical block
(the convention varies by vendor - Micron marks pages 0 and 1 of each physical
block; others use the block's last page).

Driver code typically only *reads* those markers when it builds its bad-block
table, so the two techniques compose: mark a block bad to see whether the
firmware's scan notices it, and additionally fault that block to see what
happens when the scan misses it and the block gets used anyway.

Uncorrectable ECC
~~~~~~~~~~~~~~~~~

There is currently **no way to inject an uncorrectable-ECC read.** This model
has no BCH engine: the ECC status register (``0xC30``) only distinguishes a
blank page from a written one, and reads never report correctable or
uncorrectable errors. Firmware paths that handle ECC failures cannot be
exercised here.

Tracing
-------

Useful trace events (``-trace <name>``, or ``-trace 's5l8702_nand_*'``):

``s5l8702_nand_erase``
  ``bank``, the row address the firmware asked for, and the page range
  actually cleared. Good for confirming erase addressing and, on a two-plane
  part, that erases arrive in pairs differing by the plane bit.
``s5l8702_nand_fault_injected``
  One line per injected failure.
``s5l8702_nand_read_page``
  bank, page and DMA destination for each page read.
``s5l8702_nand_reg_ecc_status``
  What the blank check decided for the last page read.
``s5l8702_fmiss_pv_unimplemented``
  An FMISS program with no paravirtualised handler.

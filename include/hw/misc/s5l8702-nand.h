#ifndef HW_MISC_S5L8702_NAND_H
#define HW_MISC_S5L8702_NAND_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "qemu/lockable.h"
#include "exec/address-spaces.h"
#include "sysemu/block-backend.h"
#include "hw/misc/s5l8702-nand-fmiss.h"

#define NAND_NUM_BANKS          8

/* Stub-mode fallback only; the real path uses geo.spare_stride from the
 * image's geometry header instead of this constant. */
#define NAND_STUB_BYTES_PER_SPARE 64

/* The FMI moves page data in fixed 2 KiB DMA/ECC sectors regardless of the
 * page size: the firmware supplies one destination (or source) address per
 * sector, and ECC status is reported per sector. */
#define NAND_SECTOR_SIZE        0x800
#define NAND_DESTADDR_QUEUE_LEN 16

#define NAND_CHIP_ID            0xA5D5D589

/* Unified backing-image geometry: banks and their spare bytes live
 * interwoven in a single BlockBackend-backed image, one page's data
 * immediately followed by its spare bytes (mirrors how a real NAND page's
 * data + OOB area sit together). The geometry is stored in the image itself
 * as a qcow2 header extension (create/stamp images with nand-image.py). */
#define NAND_GEOM_EXT_MAGIC     0x4E414E44 /* "NAND" */
#define NAND_GEOM_EXT_VERSION   2

/* On-disk payload of the geometry header extension; all fields big-endian.
 * Version 1 ends at bank_capacity (no nand_id); version 2 appends nand_id.
 * s5l8702_nand_load_geometry() accepts both, defaulting nand_id for v1. */
typedef struct QEMU_PACKED Qcow2NandGeometry {
    uint32_t version;         /* NAND_GEOM_EXT_VERSION */
    uint32_t page_size;       /* data bytes per page, e.g. 2048 */
    uint32_t spare_stride;    /* spare bytes stored per page in the image */
    uint32_t pages_per_block; /* pages erased together */
    uint32_t num_banks;       /* installed banks */
    uint64_t bank_capacity;   /* page-data bytes per bank */
    uint32_t nand_id;         /* chip ID reported by NAND_CMD_ID, v2+ */
} Qcow2NandGeometry;

/* Runtime geometry, decoded from the header extension at realize time. */
typedef struct S5L8702NandGeometry {
    uint32_t bytes_per_page;
    uint32_t spare_stride;
    uint32_t pages_per_block;
    uint32_t num_banks_installed;
    uint32_t sectors_per_page;
    uint64_t pages_per_bank;
    uint64_t page_record_size;  /* bytes_per_page + spare_stride */
    uint64_t bank_stride;       /* pages_per_bank * page_record_size */
    uint32_t nand_id;           /* chip ID reported by NAND_CMD_ID */
} S5L8702NandGeometry;

/* NAND register offsets within the 0x38A00000 MMIO region */
#define NAND_FMCTRL0    0x0
#define NAND_FMCTRL1    0x4
#define NAND_CMD        0x8
#define NAND_FMADDR0    0xC
#define NAND_FMADDR1    0x10
#define NAND_FMANUM     0x2C
#define NAND_FMDNUM     0x30
#define NAND_DESTADDR   0x34
#define NAND_FMCSTAT    0x48
#define NAND_FMFIFO     0x60
#define NAND_RSCTRL     0x100

#define FMI_PROGRAM     0xC04
#define FMI_INT         0xC0C
#define FMI_START       0xC00
#define FMI_DMEM        0xD00

#define NAND_CMD_ID              0x90
#define NAND_CMD_READ            0x30
#define NAND_CMD_READSTATUS      0x70
#define NAND_CMD_ERASE_CONFIRM   0xD0
#define NAND_CMD_PROGRAM_SETUP   0x80
#define NAND_CMD_PROGRAM_CONFIRM 0x10

#define S5L8702_NAND_BASE   0x38A00000
#define S5L8702_NAND_SIZE   0x1000

#define TYPE_S5L8702_NAND   "s5l8702-nand"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702NandState, S5L8702_NAND)

#define S5L8702_NAND_IRQ 54 // actually NAND CS IRQ, but nothing cares about NAND itself

struct S5L8702NandState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t fmctrl0;
    uint32_t fmctrl1;
    uint32_t fmaddr0;
    uint32_t fmaddr1;
    uint32_t fmanum;
    uint32_t fmdnum;
    uint32_t destaddr;
    /* The FMI scatters a page across several 2 KiB-sector destinations: the
     * firmware pushes one DESTADDR per sector before issuing a single page
     * read. Queue them so the read can deliver each sector to its own target.
     * DESTBUF auto-increments on real hardware, so the LAST queued target
     * receives everything remaining in the page (the no-ECC read program
     * supplies a single target for the whole page). */
    uint32_t destaddr_queue[NAND_DESTADDR_QUEUE_LEN];
    uint32_t destaddr_queue_count;
    uint32_t rsctrl;
    uint32_t cmd;

    uint32_t fmi_c00;
    uint32_t fmi_program;
    uint32_t fmi_int;
    uint8_t  reading_spare;

    uint8_t *page_buffer;
    union {
        uint8_t  *bytes;
        uint32_t *words;
    } page_spare_buffer;

    uint32_t buffered_bank;
    uint32_t buffered_page;
    bool     reading_multiple_pages;
    uint32_t cur_bank_reading;
    uint32_t banks_to_read[512];
    uint32_t pages_to_read[512];
    bool     is_writing;
    QemuMutex lock;

    /* "drive" qdev property: the unified NAND image (all banks + spares
     * interwoven). Its geometry header extension fills in `geo`. */
    BlockBackend *blk;
    S5L8702NandGeometry geo;

    bool fmiss_enable; // do we emulate the FMISS or paravirtualize it?

    fmiss_vm fmiss_vm;
};

void s5l8702_nand_set_buffered_page(S5L8702NandState *s, uint32_t page);

#endif /* HW_MISC_S5L8702_NAND_H */

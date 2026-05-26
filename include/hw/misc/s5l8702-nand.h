#ifndef HW_MISC_S5L8702_NAND_H
#define HW_MISC_S5L8702_NAND_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "qemu/lockable.h"
#include "exec/address-spaces.h"

/* Forward declaration – full definition is in file-cow.h (included by the .c) */
typedef struct cow_file cow_file;

#define NAND_NUM_BANKS          8
#define NAND_BYTES_PER_PAGE     2048
#define NAND_BYTES_PER_SPARE    64

#define NAND_CHIP_ID            0xA5D5D589
#define NAND_NUM_BANKS_INSTALLED 2

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

#define NAND_CMD_ID         0x90
#define NAND_CMD_READ       0x30
#define NAND_CMD_READSTATUS 0x70

#define S5L8702_NAND_BASE   0x38A00000
#define S5L8702_NAND_SIZE   0x1000

#define TYPE_S5L8702_NAND   "s5l8702-nand"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702NandState, S5L8702_NAND)

#define FMIVSS_DMEM_SIZE 32
#define S5L8702_NAND_IRQ 54 // actually NAND CS IRQ, but nothing cares about NAND itself

typedef struct {
    uint32_t regs[8];
    /* PC is an offset into the device's DMEM */
    uint32_t pc;
    uint32_t start_pc;
    uint32_t dmem[FMIVSS_DMEM_SIZE];
} fmiss_vm;

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
     * read. Queue them so the read can deliver each sector to its own target
     * (an 8 KiB page = 4 sectors). One entry == today's single-target read. */
    uint32_t destaddr_queue[16];
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

    /* nand_path is set as a qdev property; files are opened during realize */
    char     *nand_path;
    cow_file *nand_banks[NAND_NUM_BANKS];
    cow_file *nand_spares[NAND_NUM_BANKS];

    fmiss_vm fmiss_vm;
};

void s5l8702_nand_set_buffered_page(S5L8702NandState *s, uint32_t page);

#endif /* HW_MISC_S5L8702_NAND_H */

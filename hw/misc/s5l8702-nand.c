/*
 * S5L8702 NAND Flash Controller
 *
 * Ported from the qemu-ipod-nano project.
 * Reference: https://github.com/lemonjesus/S5L8702-FMISS-Tools/blob/main/Documentation.md
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "file-cow.h"
#include "hw/misc/s5l8702-nand.h"
#include "hw/misc/s5l8702-nand-fmiss.h"
#include "trace.h"
#include "hw/hw.h"
#include "hw/qdev-properties.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"

/* Forward declarations */
static uint64_t nand_mem_read(void *opaque, hwaddr addr, unsigned size);
static void nand_mem_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

static const FmissNandOps nand_fmiss_ops = {
    .read  = nand_mem_read,
    .write = nand_mem_write,
};

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

static int get_bank(S5L8702NandState *s) {
    uint32_t bank = __builtin_ctz(s->fmctrl0 >> 1);
    trace_s5l8702_nand_bank_select(s->fmctrl0, bank);
    if (bank > 7) return -1;
    return bank;
}

void s5l8702_nand_set_buffered_page(S5L8702NandState *s, uint32_t page) {
    int bank = get_bank(s);
    if (bank == -1) {
        trace_s5l8702_nand_warn_no_bank(page, s->reading_multiple_pages);
        return;
    }

    if ((uint32_t)bank != s->buffered_bank || page != s->buffered_page) {
        cow_read(s->nand_banks[bank], s->page_buffer, (uint64_t)page * NAND_BYTES_PER_PAGE, NAND_BYTES_PER_PAGE);
        cow_read(s->nand_spares[bank], s->page_spare_buffer.bytes, (uint64_t)page * 16, 12);

        s->buffered_page = page;
        s->buffered_bank = bank;
    }
}

static void s5l8702_nand_update_irq(S5L8702NandState *s) {
    /* If any interrupt flags are set, assert the IRQ. Otherwise, deassert. */
    qemu_set_irq(s->irq, s->fmi_int != 0);
}

static uint64_t nand_mem_read(void *opaque, hwaddr addr, unsigned size) {
    S5L8702NandState *s = S5L8702_NAND(opaque);
    trace_s5l8702_nand_reg_read((uint32_t)addr);

    /* DMEM window */
    if (addr >= FMI_DMEM && addr < (FMI_DMEM + 4 * FMIVSS_DMEM_SIZE)) {
        uint32_t i = (addr - FMI_DMEM) / 4;
        return s->fmiss_vm.dmem[i];
    }

    switch (addr) {
    case NAND_FMCTRL0:
        trace_s5l8702_nand_reg_fmctrl0_read(s->fmctrl0);
        return s->fmctrl0;

    case NAND_FMCTRL1:
        return s->fmctrl1;

    case NAND_FMFIFO:
        trace_s5l8702_nand_reg_fifo_read(s->cmd);
        if (s->cmd == NAND_CMD_READSTATUS) {
            return (1 << 6);
        } else {
            uint32_t page = (s->fmaddr1 << 16) | (s->fmaddr0 >> 16);
            trace_s5l8702_nand_read_page(get_bank(s), page, s->destaddr);
            s5l8702_nand_set_buffered_page(s, page);

            /* Scatter the page across the queued 2 KiB-sector destinations.
             * The firmware programs one DESTADDR per sector we must write 
             * each sector to its own target rather than dumping the whole
             * page on the last one. */
            const uint32_t sector = 0x800;
            uint32_t n = s->destaddr_queue_count;
            if (n == 0) {
                /* No queued targets (e.g. a single-sector part that didn't
                 * re-arm): fall back to the last DESTADDR. */
                s->destaddr_queue[0] = s->destaddr;
                n = 1;
            }
            for (uint32_t i = 0; i < n; i++) {
                uint32_t off = i * sector;
                if (off >= NAND_BYTES_PER_PAGE) {
                    break;
                }
                uint32_t len = sector;
                if (off + len > NAND_BYTES_PER_PAGE) {
                    len = NAND_BYTES_PER_PAGE - off;
                }
                address_space_write(&address_space_memory,
                                    s->destaddr_queue[i] ^ 0x80000000,
                                    MEMTXATTRS_UNSPECIFIED,
                                    s->page_buffer + off, len);
            }
            s->destaddr_queue_count = 0;
            trace_s5l8702_nand_reg_fifo_spare(0, s->page_spare_buffer.words[0]);
            return s->page_spare_buffer.words[0];
        }

    case 0x64:
        trace_s5l8702_nand_reg_fifo_spare(1, s->page_spare_buffer.words[1]);
        return s->page_spare_buffer.words[1];

    case 0x68:
        trace_s5l8702_nand_reg_fifo_spare(2, s->page_spare_buffer.words[2]);
        return s->page_spare_buffer.words[2];

    case 0x80:
        if (s->cmd == NAND_CMD_ID) {
            int bank = get_bank(s);
            trace_s5l8702_nand_reg_id_read(bank);
            return (bank < NAND_NUM_BANKS_INSTALLED) ? NAND_CHIP_ID : 0;
        }
        return 0xdeadbeef;

    case NAND_FMCSTAT:
        /* All banks ready */
        return 0x1FFE;

    case NAND_RSCTRL:
        return s->rsctrl;

    case FMI_PROGRAM:
        return s->fmi_program;

    case FMI_INT:
        trace_s5l8702_nand_reg_fmi_int_read(s->fmi_int);
        return s->fmi_int;

    case FMI_START:
        return 0;

    case 0xC30:
        /* ECC/blank status. Report a page as blank (0x20000000) only when BOTH
         * the spare and the data are all-0xFF, matching the rehost's blank check
         * (page_is_blank(data) && page_is_blank(spare)). Checking spare alone
         * mis-reports data-only pages (whose first spare bytes happen to be 0xFF)
         * as erased, which diverges from the rehost during the firmware's
         * VFL/FTL signature scan and makes the signature impossible to find. */
        for (uint32_t i = 0; i < 12; i++) {
            if (s->page_spare_buffer.bytes[i] != 0xFF) {
                trace_s5l8702_nand_reg_ecc_status(0);
                return 0;
            }
        }
        for (uint32_t i = 0; i < NAND_BYTES_PER_PAGE; i++) {
            if (s->page_buffer[i] != 0xFF) {
                trace_s5l8702_nand_reg_ecc_status(0);
                return 0;
            }
        }
        trace_s5l8702_nand_reg_ecc_status(1);
        return 0x20000000;

    case 0xC64:
        return 1;

    default:
        break;
    }

    trace_s5l8702_nand_read((uint32_t)addr, 0);
    return 0;
}

/* --------------------------------------------------------------------------
 * MMIO write
 * -------------------------------------------------------------------------- */

static void nand_mem_write(void *opaque, hwaddr addr, uint64_t val, unsigned size) {
    S5L8702NandState *s = S5L8702_NAND(opaque);
    trace_s5l8702_nand_write((uint32_t)addr, val);

    /* DMEM window */
    if (addr >= FMI_DMEM && addr < (FMI_DMEM + 4 * FMIVSS_DMEM_SIZE)) {
        uint32_t i = (addr - FMI_DMEM) / 4;
        s->fmiss_vm.dmem[i] = val;
        return;
    }

    switch (addr) {
    case NAND_FMCTRL0:
        s->fmctrl0 = val;
        trace_s5l8702_nand_reg_fmctrl0_write(s->fmctrl0);
        break;

    case NAND_FMCTRL1:
        s->fmctrl1 = val | (1 << 30);
        trace_s5l8702_nand_reg_fmctrl1_write(s->fmctrl1);
        break;

    case NAND_FMADDR0:
        s->fmaddr0 = val;
        break;

    case NAND_FMADDR1:
        s->fmaddr1 = val;
        break;

    case NAND_FMANUM:
        s->fmanum = val;
        break;

    case NAND_CMD:
        s->cmd = val;
        break;

    case NAND_FMDNUM:
        s->reading_spare = (val == NAND_BYTES_PER_SPARE - 1) ? 1 : 0;
        s->fmdnum = val;
        trace_s5l8702_nand_reg_fmdnum_write(s->fmdnum);
        break;

    case NAND_FMFIFO:
        if (!s->is_writing) {
            break;
        }
        ((uint32_t *)s->page_buffer)[(NAND_BYTES_PER_PAGE - s->fmdnum) / 4] = val;
        s->fmdnum -= 4;
        if (s->fmdnum == 0) {
            s->is_writing = false;
            /* Page write complete – flush to cow file */
            qemu_mutex_lock(&s->lock);
            if (s->nand_banks[s->buffered_bank]) {
                printf("[NAND] Writing page: bank=%d, page=0x%x\n", s->buffered_bank, s->buffered_page);
                cow_write(s->nand_banks[s->buffered_bank],
                          s->page_buffer,
                          (uint64_t)s->buffered_page * NAND_BYTES_PER_PAGE,
                          NAND_BYTES_PER_PAGE);
            }
            qemu_mutex_unlock(&s->lock);
        }
        break;

    case NAND_DESTADDR:
        s->destaddr = val;
        if (s->destaddr_queue_count < ARRAY_SIZE(s->destaddr_queue)) {
            s->destaddr_queue[s->destaddr_queue_count++] = val;
        }
        trace_s5l8702_nand_reg_destaddr_write(s->destaddr);
        break;

    case NAND_RSCTRL:
        s->rsctrl = val;
        break;

    case FMI_PROGRAM:
        s->fmi_program = val;
        break;

    case FMI_INT:
        s->fmi_int &= ~val;
        s5l8702_nand_update_irq(s); // Drop the IRQ line when cleared!
        break;

    case FMI_START:
        if (val == 0xfff5) {
            fmiss_vm_reset(&s->fmiss_vm, s->fmi_program);
            fmiss_vm_execute(&nand_fmiss_ops, opaque, &s->fmiss_vm);
            s->fmi_int |= 1;
            s5l8702_nand_update_irq(s); // Raise the IRQ line
        }
        break;

    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * MemoryRegionOps
 * -------------------------------------------------------------------------- */

static const MemoryRegionOps nand_ops = {
    .read       = nand_mem_read,
    .write      = nand_mem_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* --------------------------------------------------------------------------
 * Device lifecycle
 * -------------------------------------------------------------------------- */

static void s5l8702_nand_init(Object *obj) {
    trace_s5l8702_nand_init();
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    S5L8702NandState *s = S5L8702_NAND(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &nand_ops, s, "s5l8702-nand", S5L8702_NAND_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    s->page_buffer = g_malloc(NAND_BYTES_PER_PAGE);
    s->page_spare_buffer.bytes = g_malloc(NAND_BYTES_PER_SPARE);
    memset(s->page_spare_buffer.bytes, 0xff, NAND_BYTES_PER_SPARE);
    s->buffered_page = -1;
    s->buffered_bank = -1;

    fmiss_vm_reset(&s->fmiss_vm, 0);

    qemu_mutex_init(&s->lock);
}

static void s5l8702_nand_realize(DeviceState *dev, Error **errp) {
    S5L8702NandState *s = S5L8702_NAND(dev);
    trace_s5l8702_nand_realize(s->nand_path ? s->nand_path : "(none)");

    if (!s->nand_path) {
        /* No path provided – operate without backing files (stub mode) */
        return;
    }

    for (int i = 0; i < NAND_NUM_BANKS; i++) {
        char *path = g_strdup_printf("%s/bank%d.bin", s->nand_path, i);
        s->nand_banks[i] = cow_open(path);
        if (!s->nand_banks[i]) {
            warn_report("s5l8702-nand: could not open %s", path);
        }
        g_free(path);

        char *spare_path = g_strdup_printf("%s/spare%d.bin", s->nand_path, i);
        s->nand_spares[i] = cow_open(spare_path);
        if (!s->nand_spares[i]) {
            warn_report("s5l8702-nand: could not open %s", spare_path);
        }
        g_free(spare_path);
    }
}

static void s5l8702_nand_reset(DeviceState *dev) {
    trace_s5l8702_nand_reset();
    S5L8702NandState *s = S5L8702_NAND(dev);

    s->fmctrl0       = 0;
    s->fmctrl1       = 0;
    s->fmaddr0       = 0;
    s->fmaddr1       = 0;
    s->fmanum        = 0;
    s->fmdnum        = 0;
    s->rsctrl        = 0;
    s->cmd           = 0;
    s->fmi_program   = 0;
    s->fmi_int       = 0;
    s->reading_spare = 0;
    s->buffered_page = -1;
    s->destaddr_queue_count = 0;

    fmiss_vm_reset(&s->fmiss_vm, 0);
    
    s5l8702_nand_update_irq(s); // Ensure line is low on boot
}

static Property s5l8702_nand_properties[] = {
    DEFINE_PROP_STRING("nand-path", S5L8702NandState, nand_path),
    DEFINE_PROP_END_OF_LIST(),
};

static void s5l8702_nand_class_init(ObjectClass *oc, void *data) {
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = s5l8702_nand_realize;
    dc->reset   = s5l8702_nand_reset;
    device_class_set_props(dc, s5l8702_nand_properties);
}

static const TypeInfo s5l8702_nand_info = {
    .name          = TYPE_S5L8702_NAND,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S5L8702NandState),
    .instance_init = s5l8702_nand_init,
    .class_init    = s5l8702_nand_class_init,
};

static void s5l8702_nand_register_types(void) {
    type_register_static(&s5l8702_nand_info);
}

type_init(s5l8702_nand_register_types)

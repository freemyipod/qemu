/*
 * S5L8702 NAND Flash Controller
 *
 * Ported from the qemu-ipod-nano project.
 * Reference: https://github.com/lemonjesus/S5L8702-FMISS-Tools/blob/main/Documentation.md
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/misc/s5l8702-nand.h"
#include "hw/misc/s5l8702-nand-fmiss.h"
#include "hw/misc/s5l8702-nand-fmiss-pv.h"
#include "trace.h"
#include "hw/hw.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "sysemu/block-backend.h"

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

/* Offsets into the unified backing image: banks and their spare bytes are
 * interwoven, one page's data immediately followed by its spare bytes. */
static uint64_t nand_page_data_offset(S5L8702NandState *s, uint32_t bank, uint32_t page) {
    return (uint64_t)bank * s->geo.bank_stride + (uint64_t)page * s->geo.page_record_size;
}

static uint64_t nand_page_spare_offset(S5L8702NandState *s, uint32_t bank, uint32_t page) {
    return nand_page_data_offset(s, bank, page) + s->geo.bytes_per_page;
}

void s5l8702_nand_set_buffered_page(S5L8702NandState *s, uint32_t page) {
    int bank = get_bank(s);
    if (bank == -1) {
        trace_s5l8702_nand_warn_no_bank(page, s->reading_multiple_pages);
        return;
    }
    if (!s->blk || (uint32_t)bank >= s->geo.num_banks_installed) {
        return;
    }

    if ((uint32_t)bank != s->buffered_bank || page != s->buffered_page) {
        blk_pread(s->blk, nand_page_data_offset(s, bank, page), s->geo.bytes_per_page, s->page_buffer, 0);
        /* Read the full spare record (image's spare_stride), not just the
         * 12 metadata bytes the controller tracks; truncating drops the
         * ECC a real chip's dump carries past byte 12. */
        blk_pread(s->blk, nand_page_spare_offset(s, bank, page), s->geo.spare_stride,
                  s->page_spare_buffer.bytes, 0);

        s->buffered_page = page;
        s->buffered_bank = bank;
    }
}

static void s5l8702_nand_update_irq(S5L8702NandState *s) {
    /* If any interrupt flags are set, assert the IRQ. Otherwise, deassert. */
    qemu_set_irq(s->irq, s->fmi_int != 0);
}

static uint32_t s5l8702_nand_current_page(S5L8702NandState *s) {
    return (s->fmaddr1 << 16) | (s->fmaddr0 >> 16);
}

static void s5l8702_nand_do_erase(S5L8702NandState *s) {
    int bank = get_bank(s);
    if (bank == -1 || !s->blk || (uint32_t)bank >= s->geo.num_banks_installed) {
        return;
    }

    uint32_t block = s->fmaddr0;
    uint32_t page0 = block * s->geo.pages_per_block;

    g_autofree uint8_t *erased_record = g_malloc(s->geo.page_record_size);
    memset(erased_record, 0xff, s->geo.page_record_size);

    qemu_mutex_lock(&s->lock);
    for (uint32_t i = 0; i < s->geo.pages_per_block; i++) {
        blk_pwrite(s->blk, nand_page_data_offset(s, bank, page0 + i), s->geo.page_record_size, erased_record, 0);
    }
    qemu_mutex_unlock(&s->lock);

    if (s->buffered_bank == (uint32_t)bank &&
        s->buffered_page >= page0 && s->buffered_page < page0 + s->geo.pages_per_block) {
        s->buffered_page = -1;
    }
}

static void s5l8702_nand_do_program(S5L8702NandState *s) {
    int bank = get_bank(s);
    if (bank == -1 || !s->blk || (uint32_t)bank >= s->geo.num_banks_installed) {
        s->destaddr_queue_count = 0;
        return;
    }

    uint32_t page = s5l8702_nand_current_page(s);

    const uint32_t sector = NAND_SECTOR_SIZE;
    uint32_t n = s->destaddr_queue_count;
    if (n == 0) {
        s->destaddr_queue[0] = s->destaddr;
        n = 1;
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t off = i * sector;
        if (off >= s->geo.bytes_per_page) {
            break;
        }
        uint32_t len = sector;
        /* DESTBUF auto-increments: the last queued source supplies the
         * rest of the page (a single source == a whole-page transfer). */
        if (i == n - 1 || off + len > s->geo.bytes_per_page) {
            len = s->geo.bytes_per_page - off;
        }
        address_space_read(&address_space_memory, s->destaddr_queue[i] ^ 0x80000000, MEMTXATTRS_UNSPECIFIED, s->page_buffer + off, len);
    }
    s->destaddr_queue_count = 0;

    qemu_mutex_lock(&s->lock);
    blk_pwrite(s->blk, nand_page_data_offset(s, bank, page), s->geo.bytes_per_page, s->page_buffer, 0);
    /* Write the full spare record too, or the rest holds stale data from
     * whatever occupied the page before (0xFF if erased, stale ECC if not). */
    blk_pwrite(s->blk, nand_page_spare_offset(s, bank, page), s->geo.spare_stride,
               s->page_spare_buffer.bytes, 0);
    qemu_mutex_unlock(&s->lock);

    s->buffered_bank = bank;
    s->buffered_page = page;
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
            const uint32_t sector = NAND_SECTOR_SIZE;
            uint32_t n = s->destaddr_queue_count;
            if (n == 0) {
                /* No queued targets (e.g. a single-sector part that didn't
                 * re-arm): fall back to the last DESTADDR. */
                s->destaddr_queue[0] = s->destaddr;
                n = 1;
            }
            for (uint32_t i = 0; i < n; i++) {
                uint32_t off = i * sector;
                if (off >= s->geo.bytes_per_page) {
                    break;
                }
                uint32_t len = sector;
                /* DESTBUF auto-increments: the last queued target receives
                 * everything remaining in the page (the no-ECC read program
                 * supplies a single target for the whole page). */
                if (i == n - 1 || off + len > s->geo.bytes_per_page) {
                    len = s->geo.bytes_per_page - off;
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
            return (bank >= 0 && (uint32_t)bank < s->geo.num_banks_installed) ? s->geo.nand_id : 0;
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
         * VFL/FTL signature scan and makes the signature impossible to find.
         *
         * The scan covers the whole spare_stride record, not just the first
         * 12 bytes, since a written page can carry ECC past byte 12. */
        for (uint32_t i = 0; i < s->geo.spare_stride; i++) {
            if (s->page_spare_buffer.bytes[i] != 0xFF) {
                trace_s5l8702_nand_reg_ecc_status(0);
                return 0;
            }
        }
        for (uint32_t i = 0; i < s->geo.bytes_per_page; i++) {
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
        if (val == NAND_CMD_ERASE_CONFIRM) {
            s5l8702_nand_do_erase(s);
        } else if (val == NAND_CMD_PROGRAM_CONFIRM) {
            s5l8702_nand_do_program(s);
        }
        break;

    case NAND_FMDNUM:
        /* FMDNUM carries (transfer length - 1); firmware sets it to the
         * spare size for a spare-only transfer, so compare against
         * spare_stride rather than a fixed constant. */
        s->reading_spare = (val == s->geo.spare_stride - 1) ? 1 : 0;
        s->fmdnum = val;
        trace_s5l8702_nand_reg_fmdnum_write(s->fmdnum);
        break;

    case NAND_FMFIFO:
        s->page_spare_buffer.words[0] = val;
        if (!s->is_writing) {
            break;
        }
        ((uint32_t *)s->page_buffer)[(s->geo.bytes_per_page - s->fmdnum) / 4] = val;
        s->fmdnum -= 4;
        if (s->fmdnum == 0) {
            s->is_writing = false;
            /* Page write complete: flush to the backing image */
            qemu_mutex_lock(&s->lock);
            if (s->blk && s->buffered_bank < s->geo.num_banks_installed) {
                printf("[NAND] Writing page: bank=%d, page=0x%x\n", s->buffered_bank, s->buffered_page);
                blk_pwrite(s->blk, nand_page_data_offset(s, s->buffered_bank, s->buffered_page), s->geo.bytes_per_page, s->page_buffer, 0);
            }
            qemu_mutex_unlock(&s->lock);
        }
        break;

    case 0x64:
        s->page_spare_buffer.words[1] = val;
        break;

    case 0x68:
        s->page_spare_buffer.words[2] = val;
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
            if (s->fmiss_enable) {
                fmiss_vm_reset(&s->fmiss_vm, s->fmi_program);
                fmiss_vm_execute(&nand_fmiss_ops, opaque, &s->fmiss_vm);
            } else {
                FmissPvContext pv_ctx = {
                    .opaque           = opaque,
                    .ops              = &nand_fmiss_ops,
                    .program_addr     = s->fmi_program,
                    .sectors_per_page = s->geo.sectors_per_page,
                };
                fmiss_pv_dispatch(&pv_ctx);
            }
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

    s->buffered_page = -1;
    s->buffered_bank = -1;

    fmiss_vm_reset(&s->fmiss_vm, 0);

    qemu_mutex_init(&s->lock);
}

/* Geometry for stub mode (no drive attached); never used to address an image. */
static const S5L8702NandGeometry nand_stub_geometry = {
    .bytes_per_page      = 2048,
    .spare_stride        = NAND_STUB_BYTES_PER_SPARE,
    .pages_per_block     = 128,
    .num_banks_installed = 2,
    .nand_id             = NAND_CHIP_ID,
};

static bool s5l8702_nand_load_geometry(S5L8702NandState *s, Error **errp) {
    Qcow2NandGeometry ext;
    memset(&ext, 0, sizeof(ext));
    int ret = blk_get_header_ext(s->blk, NAND_GEOM_EXT_MAGIC, &ext, sizeof(ext));

    if (ret == -ENOENT) {
        error_setg(errp, "NAND image '%s' has no geometry header extension; "
                   "create it with nand-image.py, or stamp an existing image "
                   "with 'nand-image.py stamp'", blk_name(s->blk));
        return false;
    }
    if (ret < 0) {
        error_setg_errno(errp, -ret, "failed to read NAND geometry header extension");
        return false;
    }

    uint32_t version = be32_to_cpu(ext.version);
    /* v1 images predate nand_id and are shorter (no trailing field); accept
     * them and fall back to the compiled-in chip ID. */
    size_t min_len = (version == 1) ? offsetof(Qcow2NandGeometry, nand_id)
                                     : sizeof(ext);
    if (version != 1 && version != NAND_GEOM_EXT_VERSION) {
        error_setg(errp, "unsupported NAND geometry extension version %u", version);
        return false;
    }
    if ((size_t)ret < min_len) {
        error_setg(errp, "NAND geometry header extension too short (%d bytes)", ret);
        return false;
    }

    S5L8702NandGeometry *g = &s->geo;
    g->bytes_per_page      = be32_to_cpu(ext.page_size);
    g->spare_stride        = be32_to_cpu(ext.spare_stride);
    g->pages_per_block     = be32_to_cpu(ext.pages_per_block);
    g->num_banks_installed = be32_to_cpu(ext.num_banks);
    g->nand_id             = (version >= 2) ? be32_to_cpu(ext.nand_id) : NAND_CHIP_ID;
    uint64_t bank_capacity = be64_to_cpu(ext.bank_capacity);

    if (g->bytes_per_page == 0 || g->bytes_per_page % NAND_SECTOR_SIZE != 0) {
        error_setg(errp, "NAND page size %u is not a multiple of the %u-byte "
                   "FMI sector", g->bytes_per_page, (uint32_t)NAND_SECTOR_SIZE);
        return false;
    }
    if (g->spare_stride < 12) {
        error_setg(errp, "NAND spare stride %u is too small (the controller "
                   "stores 12 metadata bytes per page)", g->spare_stride);
        return false;
    }
    if (g->pages_per_block == 0) {
        error_setg(errp, "NAND pages-per-block must be non-zero");
        return false;
    }
    if (g->num_banks_installed == 0 || g->num_banks_installed > NAND_NUM_BANKS) {
        error_setg(errp, "NAND bank count %u out of range (1..%u)",
                   g->num_banks_installed, (uint32_t)NAND_NUM_BANKS);
        return false;
    }
    if (bank_capacity == 0 || bank_capacity % g->bytes_per_page != 0) {
        error_setg(errp, "NAND bank capacity %" PRIu64 " is not a multiple of "
                   "the page size %u", bank_capacity, g->bytes_per_page);
        return false;
    }

    g->sectors_per_page = g->bytes_per_page / NAND_SECTOR_SIZE;
    g->pages_per_bank   = bank_capacity / g->bytes_per_page;
    g->page_record_size = (uint64_t)g->bytes_per_page + g->spare_stride;
    g->bank_stride      = g->pages_per_bank * g->page_record_size;

    int64_t len = blk_getlength(s->blk);
    if (len < 0) {
        error_setg_errno(errp, -len, "failed to get NAND image length");
        return false;
    }
    uint64_t required = g->bank_stride * g->num_banks_installed;
    if ((uint64_t)len < required) {
        error_setg(errp, "NAND image is %" PRId64 " bytes but the geometry "
                   "extension describes %" PRIu64 " bytes "
                   "(%u banks * %" PRIu64 " pages * %" PRIu64 "-byte records)",
                   len, required, g->num_banks_installed, g->pages_per_bank,
                   g->page_record_size);
        return false;
    }

    return true;
}

static void s5l8702_nand_realize(DeviceState *dev, Error **errp) {
    S5L8702NandState *s = S5L8702_NAND(dev);
    trace_s5l8702_nand_realize(s->blk ? blk_name(s->blk) : "(none)");

    if (!s->blk) {
        /* No drive attached: operate without backing storage (stub mode) */
        s->geo = nand_stub_geometry;
        s->geo.sectors_per_page = s->geo.bytes_per_page / NAND_SECTOR_SIZE;
        s->geo.page_record_size = s->geo.bytes_per_page + s->geo.spare_stride;
    } else {
        if (!s5l8702_nand_load_geometry(s, errp)) {
            return;
        }

        uint64_t perm = BLK_PERM_CONSISTENT_READ | (blk_supports_write_perm(s->blk) ? BLK_PERM_WRITE : 0);
        if (blk_set_perm(s->blk, perm, BLK_PERM_ALL, errp) < 0) return;
    }

    s->page_buffer = g_malloc(s->geo.bytes_per_page);
    /* Sized from the image's spare_stride, not a fixed constant: some
     * parts' stride (448) is far past the old fixed 64-byte allocation. */
    s->page_spare_buffer.bytes = g_malloc(s->geo.spare_stride);
    memset(s->page_spare_buffer.bytes, 0xff, s->geo.spare_stride);
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
    DEFINE_PROP_DRIVE("drive", S5L8702NandState, blk),
    DEFINE_PROP_BOOL("fmiss-enable", S5L8702NandState, fmiss_enable, false),
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

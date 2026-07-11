/*
 * S5L8702 NAND FMISS paravirtualization
 *
 * See s5l8702-nand-fmiss-pv.h for the rationale. This file owns the table
 * of known FMISS programs and their handwritten replacements.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "hw/misc/s5l8702-nand.h"
#include "hw/misc/s5l8702-nand-fmiss-pv.h"
#include "trace.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"

// algorithm for hashing provided programs
static uint64_t fnv1a64(const uint8_t *data, size_t len) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

uint64_t fmiss_pv_hash_program(uint32_t program_addr) {
    uint8_t buf[FMISS_PV_HASH_WINDOW];
    address_space_read(&address_space_memory, program_addr, MEMTXATTRS_UNSPECIFIED, buf, sizeof(buf));
    return fnv1a64(buf, sizeof(buf));
}

// memory helpers
static uint32_t dmem_read(const FmissPvContext *ctx, int word) {
    return ctx->ops->read(ctx->opaque, FMI_DMEM + word * 4, 4);
}

static void dmem_write(const FmissPvContext *ctx, int word, uint32_t val) {
    ctx->ops->write(ctx->opaque, FMI_DMEM + word * 4, val, 4);
}

static uint32_t guest_read32(uint32_t addr) {
    uint32_t val = 0;
    address_space_read(&address_space_memory, addr ^ 0x80000000, MEMTXATTRS_UNSPECIFIED, &val, 4);
    return val;
}

static void guest_write32(uint32_t addr, uint32_t val) {
    address_space_write(&address_space_memory, addr & ~0x80000000u, MEMTXATTRS_UNSPECIFIED, &val, 4);
}

// helper to walk lists of pointers
static uint32_t pv_consume(const FmissPvContext *ctx, int list_word) {
    uint32_t ptr = dmem_read(ctx, list_word);
    uint32_t val = guest_read32(ptr);
    dmem_write(ctx, list_word, ptr + 4);
    return val;
}

static void pv_select_bank(const FmissPvContext *ctx, uint32_t bank) {
    ctx->ops->write(ctx->opaque, NAND_FMCTRL0, 2u << bank, 4);
}

// perform a lookup into the bank table to get the physical bank number
static uint32_t pv_next_bank_via_table(const FmissPvContext *ctx) {
    uint32_t bank_id = pv_consume(ctx, 4);
    uint32_t table_base = dmem_read(ctx, 5);
    return guest_read32(table_base + bank_id * 4);
}

static uint32_t pv_direct_addr(const FmissPvContext *ctx, int addr_word) {
    return dmem_read(ctx, addr_word);
}

static void pv_write_result(const FmissPvContext *ctx, int result_ptr_word,
                            uint32_t status) {
    uint32_t ptr = dmem_read(ctx, result_ptr_word);
    guest_write32(ptr, status);
    dmem_write(ctx, result_ptr_word, ptr + 4);
}

static uint32_t pv_sectors_per_page(const FmissPvContext *ctx) {
    uint32_t sectors = dmem_read(ctx, 10);
    if (sectors != ctx->sectors_per_page) {
        warn_report_once("s5l8702-nand: firmware set %u sectors/page (DMEM "
                         "0xD28) but geometry says %u; trusting the firmware",
                         sectors, ctx->sectors_per_page);
    }
    if (sectors == 0 || sectors > NAND_DESTADDR_QUEUE_LEN) {
        sectors = ctx->sectors_per_page;
    }
    return sectors;
}

/* -----------------------------------------------------------------------
 * READ ID
 *
 * 0xD08: destination pointer, one uint32 chip ID written per bank
 * 0xD0C: bank count (plain scalar, not a pointer)
 * 0xD10: bank-id list pointer (no table translation)
 * -------------------------------------------------------------------- */
static void pv_read_id(const FmissPvContext *ctx) {
    uint32_t count = dmem_read(ctx, 3);

    for (uint32_t i = 0; i < count; i++) {
        uint32_t bank = pv_consume(ctx, 4);
        pv_select_bank(ctx, bank);
        ctx->ops->write(ctx->opaque, NAND_CMD, NAND_CMD_ID, 4);
        uint32_t id = ctx->ops->read(ctx->opaque, 0x80, 4);

        uint32_t dest = dmem_read(ctx, 2);
        guest_write32(dest, id);
        dmem_write(ctx, 2, dest + 4);

        trace_s5l8702_fmiss_pv_read_id(bank, id);
    }
}

/* -----------------------------------------------------------------------
 * ERASE
 *
 * 0xD0C:       block-address list pointer (row address is block-only;
 *                the real program issues FMC_ANUM=2, i.e. no page/column
 *                component)
 * 0xD10/0xD14: bank-id list + translation table
 * 0xD18:       number of blocks to erase in this call
 * -------------------------------------------------------------------- */
static void pv_erase(const FmissPvContext *ctx) {
    uint32_t count = dmem_read(ctx, 6);

    for (uint32_t i = 0; i < count; i++) {
        uint32_t bank = pv_next_bank_via_table(ctx);
        pv_select_bank(ctx, bank);

        uint32_t block = pv_consume(ctx, 3);
        ctx->ops->write(ctx->opaque, NAND_FMANUM, 2, 4);
        ctx->ops->write(ctx->opaque, NAND_FMADDR0, block, 4);
        ctx->ops->write(ctx->opaque, NAND_CMD, NAND_CMD_ERASE_CONFIRM, 4);

        trace_s5l8702_fmiss_pv_erase(bank, block);
    }
}

/* -----------------------------------------------------------------------
 * READ WITHOUT ECC
 *
 * 0xD0C:       row-address (page) list pointer
 * 0xD10/0xD14: bank-id list + translation table
 * 0xD20:       destination pointer list: a single data-buffer entry for
 *                the whole page (DESTBUF auto-increments), then the spare
 *                buffer, pulled through the FIFO words like read/write.
 * -------------------------------------------------------------------- */
static void pv_read_noecc(const FmissPvContext *ctx) {
    uint32_t bank = pv_next_bank_via_table(ctx);
    pv_select_bank(ctx, bank);

    uint32_t page = pv_consume(ctx, 3);
    ctx->ops->write(ctx->opaque, NAND_FMANUM, 4, 4);
    ctx->ops->write(ctx->opaque, NAND_FMADDR0, page << 16, 4);
    ctx->ops->write(ctx->opaque, NAND_FMADDR1, page >> 16, 4);
    ctx->ops->write(ctx->opaque, NAND_CMD, NAND_CMD_READ, 4);

    uint32_t data_dest = pv_consume(ctx, 8);
    ctx->ops->write(ctx->opaque, NAND_DESTADDR, data_dest, 4);
    uint32_t spare0 = ctx->ops->read(ctx->opaque, NAND_FMFIFO, 4);

    uint32_t spare_dest = pv_consume(ctx, 8);
    if (spare_dest) {
        guest_write32(spare_dest,     spare0);
        guest_write32(spare_dest + 4, ctx->ops->read(ctx->opaque, 0x64, 4));
        guest_write32(spare_dest + 8, ctx->ops->read(ctx->opaque, 0x68, 4));
    }

    trace_s5l8702_fmiss_pv_read(bank, page, data_dest);
}

/* -----------------------------------------------------------------------
 * READ PAGE WITH ECC (but there's no actual ECC)
 *
 * 0xD18:       number of (bank, page) transfers in this call
 * 0xD0C:       row-address (page) list pointer
 * 0xD10/0xD14: bank-id list + translation table
 * 0xD1C:       spare-word destination: a direct address, advanced by 12
 *                bytes (3 FIFO words) after each transfer
 * 0xD20:       data destination pointer list, one entry per 2 KiB sector
 * 0xD24:       ECC/status result word list, one entry per transfer
 *                (0x20000000 flags a blank page)
 * 0xD28:       sectors per page
 * -------------------------------------------------------------------- */
static void pv_read(const FmissPvContext *ctx) {
    uint32_t count = dmem_read(ctx, 6);
    uint32_t sectors = pv_sectors_per_page(ctx);

    for (uint32_t i = 0; i < count; i++) {
        uint32_t bank = pv_next_bank_via_table(ctx);
        pv_select_bank(ctx, bank);

        uint32_t page = pv_consume(ctx, 3);
        ctx->ops->write(ctx->opaque, NAND_FMANUM, 4, 4);
        ctx->ops->write(ctx->opaque, NAND_FMADDR0, page << 16, 4);
        ctx->ops->write(ctx->opaque, NAND_FMADDR1, page >> 16, 4);
        ctx->ops->write(ctx->opaque, NAND_CMD, NAND_CMD_READ, 4);

        uint32_t data_dest = 0;
        for (uint32_t sect = 0; sect < sectors; sect++) {
            uint32_t dest = pv_consume(ctx, 8);
            if (sect == 0) {
                data_dest = dest;
            }
            ctx->ops->write(ctx->opaque, NAND_DESTADDR, dest, 4);
        }
        uint32_t spare0 = ctx->ops->read(ctx->opaque, NAND_FMFIFO, 4);

        uint32_t spare_dest = pv_direct_addr(ctx, 7);
        if (spare_dest) {
            guest_write32(spare_dest,     spare0);
            guest_write32(spare_dest + 4, ctx->ops->read(ctx->opaque, 0x64, 4));
            guest_write32(spare_dest + 8, ctx->ops->read(ctx->opaque, 0x68, 4));
            dmem_write(ctx, 7, spare_dest + 12);
        }

        /* Report the blank flag via 0xC30, which the FTL uses to tell
         * erased pages from data. */
        pv_write_result(ctx, 9, ctx->ops->read(ctx->opaque, 0xC30, 4));

        trace_s5l8702_fmiss_pv_read(bank, page, data_dest);
    }
}

/* -----------------------------------------------------------------------
 * WRITE PAGE
 *
 * 0xD0C:       row-address (page) list pointer
 * 0xD10/0xD14: bank-id list + translation table
 * 0xD18:       number of (bank, page) transfers in this call
 * 0xD1C:       spare-word source: a direct address, advanced by 12
 *                bytes (3 FIFO words) after each transfer
 * 0xD20:       data source pointer list, one entry per 2 KiB sector
 *                (DESTADDR reused as a source address when programming)
 * 0xD28:       sectors per page
 * -------------------------------------------------------------------- */
static void pv_write(const FmissPvContext *ctx) {
    uint32_t count = dmem_read(ctx, 6);
    uint32_t sectors = pv_sectors_per_page(ctx);

    for (uint32_t i = 0; i < count; i++) {
        uint32_t bank = pv_next_bank_via_table(ctx);
        pv_select_bank(ctx, bank);

        uint32_t data_src = 0;
        for (uint32_t sect = 0; sect < sectors; sect++) {
            uint32_t src = pv_consume(ctx, 8);
            if (sect == 0) {
                data_src = src;
            }
            ctx->ops->write(ctx->opaque, NAND_DESTADDR, src, 4);
        }

        uint32_t spare_src = pv_direct_addr(ctx, 7);
        if (spare_src) {
            ctx->ops->write(ctx->opaque, NAND_FMFIFO, guest_read32(spare_src), 4);
            ctx->ops->write(ctx->opaque, 0x64, guest_read32(spare_src + 4), 4);
            ctx->ops->write(ctx->opaque, 0x68, guest_read32(spare_src + 8), 4);
            dmem_write(ctx, 7, spare_src + 12);
        }

        uint32_t page = pv_consume(ctx, 3);
        ctx->ops->write(ctx->opaque, NAND_FMANUM, 4, 4);
        ctx->ops->write(ctx->opaque, NAND_FMADDR0, page << 16, 4);
        ctx->ops->write(ctx->opaque, NAND_FMADDR1, page >> 16, 4);
        ctx->ops->write(ctx->opaque, NAND_CMD, NAND_CMD_PROGRAM_CONFIRM, 4);

        trace_s5l8702_fmiss_pv_write(bank, page, data_src);
    }
}

/* -----------------------------------------------------------------------
 * null fn - literally just the terminate instruction
 * -------------------------------------------------------------------- */
static void pv_noop(const FmissPvContext *ctx) {}

/* -----------------------------------------------------------------------
 * Dispatch table
 *
 * Hashes are FNV-1a64 over the first FMISS_PV_HASH_WINDOW bytes of each
 * known program's bytecode.
 * -------------------------------------------------------------------- */
static const FmissPvEntry fmiss_pv_table[] = {
    { 0x8D331631EC7EB0A9ULL, "erase",       pv_erase,      false },
    { 0x50D27BA0981C03DCULL, "read",        pv_read,       false },
    { 0xB1325C82A084ECFCULL, "read_id",     pv_read_id,    false },
    { 0xA9756E8B98D35304ULL, "read_noecc",  pv_read_noecc, false },
    { 0x4A809424EDFF2B18ULL, "write",       pv_write,      true },
    { 0xF4CEA8272152513DULL, "write_cache",                  pv_write, true },
    { 0xCC463A75F535E0F2ULL, "write_2plane",                 pv_write, true },
    { 0x28C1B506523C4367ULL, "write_2plane_cache_interleave", pv_write, true },
    { 0x3C7A54540F04F991ULL, "write_cache_interleave",       pv_write, true },
    { 0xFC5D27502C385000ULL, "noop",        pv_noop,       false },
};

void fmiss_pv_dispatch(const FmissPvContext *ctx) {
    uint64_t hash = fmiss_pv_hash_program(ctx->program_addr);

    for (size_t i = 0; i < ARRAY_SIZE(fmiss_pv_table); i++) {
        if (fmiss_pv_table[i].hash == hash) {
            // trace_s5l8702_fmiss_pv_matched(fmiss_pv_table[i].name, hash);
            fmiss_pv_table[i].handler(ctx);
            return;
        }
    }

    trace_s5l8702_fmiss_pv_unimplemented(hash, ctx->program_addr);
    warn_report("s5l8702-nand: unrecognized FMISS program at 0x%08x (hash=0x%016" PRIx64 "); no paravirtualized handler for it yet", ctx->program_addr, hash);
}

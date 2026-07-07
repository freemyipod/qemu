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

// we don't need ecc where we're going!
static void pv_write_result_ok(const FmissPvContext *ctx, int result_ptr_word) {
    uint32_t ptr = dmem_read(ctx, result_ptr_word);
    guest_write32(ptr, 0);
    dmem_write(ctx, result_ptr_word, ptr + 4);
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
 * 0xD20:       destination pointer list: data buffer, then spare buffer.
 *                We pull the spare bytes through the FIFO words rather
 *                than DMAing them, same as the read/write paths below.
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
 * 0xD20:       data destination pointer list, one entry per transfer
 * 0xD24:       per-sector ECC/status result word list
 * -------------------------------------------------------------------- */
static void pv_read(const FmissPvContext *ctx) {
    uint32_t count = dmem_read(ctx, 6);

    for (uint32_t i = 0; i < count; i++) {
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

        uint32_t spare_dest = pv_direct_addr(ctx, 7);
        if (spare_dest) {
            guest_write32(spare_dest,     spare0);
            guest_write32(spare_dest + 4, ctx->ops->read(ctx->opaque, 0x64, 4));
            guest_write32(spare_dest + 8, ctx->ops->read(ctx->opaque, 0x68, 4));
            dmem_write(ctx, 7, spare_dest + 12);
        }

        // no ecc today!
        pv_write_result_ok(ctx, 9);

        trace_s5l8702_fmiss_pv_read(bank, page, data_dest);
    }
}

/* -----------------------------------------------------------------------
 * WRITE PAGE
 *
 * 0xD0C:       row-address (page) list pointer
 * 0xD10/0xD14: bank-id list + translation table
 * 0xD1C:       spare-word source: a direct address, advanced by 12
 *                bytes (3 FIFO words) after each transfer
 * 0xD20:       data source pointer list (DESTADDR reused as a source
 *                address when programming)
 * -------------------------------------------------------------------- */
static void pv_write(const FmissPvContext *ctx) {
    uint32_t bank = pv_next_bank_via_table(ctx);
    pv_select_bank(ctx, bank);

    uint32_t data_src = pv_consume(ctx, 8);
    ctx->ops->write(ctx->opaque, NAND_DESTADDR, data_src, 4);

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
    { 0x8D331631EC7EB0A9ULL, "erase",       pv_erase },
    { 0x50D27BA0981C03DCULL, "read",        pv_read },
    { 0xB1325C82A084ECFCULL, "read_id",     pv_read_id },
    { 0xA9756E8B98D35304ULL, "read_noecc",  pv_read_noecc },
    { 0x4A809424EDFF2B18ULL, "write",       pv_write },
    { 0xFC5D27502C385000ULL, "noop",        pv_noop },
};

void fmiss_pv_dispatch(const FmissPvContext *ctx) {
    uint64_t hash = fmiss_pv_hash_program(ctx->program_addr);

    for (size_t i = 0; i < ARRAY_SIZE(fmiss_pv_table); i++) {
        if (fmiss_pv_table[i].hash == hash) {
            trace_s5l8702_fmiss_pv_matched(fmiss_pv_table[i].name, hash);
            fmiss_pv_table[i].handler(ctx);
            return;
        }
    }

    trace_s5l8702_fmiss_pv_unimplemented(hash, ctx->program_addr);
    warn_report("s5l8702-nand: unrecognized FMISS program at 0x%08x (hash=0x%016" PRIx64 "); no paravirtualized handler for it yet", ctx->program_addr, hash);
}

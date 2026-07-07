/*
 * S5L8702 NAND FMISS paravirtualization.
 * Recognizes known FMISS programs by hashing their first bytes and jumps
 * straight to a handwritten implementation instead of running the full
 * (experimental) micro-VM emulation.
 */

#ifndef HW_MISC_S5L8702_NAND_FMISS_PV_H
#define HW_MISC_S5L8702_NAND_FMISS_PV_H

#include "qemu/osdep.h"
#include "exec/hwaddr.h"
#include "hw/misc/s5l8702-nand-fmiss.h"

#define FMISS_PV_HASH_WINDOW 256 // differentiate programs by their first 256 bytes

typedef struct FmissPvContext {
    void *opaque;
    const FmissNandOps *ops;
    uint32_t program_addr;
} FmissPvContext;

typedef void (*fmiss_pv_handler_fn)(const FmissPvContext *ctx);

typedef struct FmissPvEntry {
    uint64_t hash;
    const char *name;
    fmiss_pv_handler_fn handler;
} FmissPvEntry;

uint64_t fmiss_pv_hash_program(uint32_t program_addr);

void fmiss_pv_dispatch(const FmissPvContext *ctx);

#endif /* HW_MISC_S5L8702_NAND_FMISS_PV_H */

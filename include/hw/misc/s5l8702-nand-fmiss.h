/* S5L8702 NAND FMISS micro-VM */

#ifndef HW_MISC_S5L8702_NAND_FMISS_H
#define HW_MISC_S5L8702_NAND_FMISS_H

#include "qemu/osdep.h"
#include "exec/hwaddr.h"

#define FMIVSS_DMEM_SIZE 32

typedef struct fmiss_vm {
    uint32_t regs[8];
    /* PC is an offset into the device's DMEM */
    uint32_t pc;
    uint32_t start_pc;
    uint32_t dmem[FMIVSS_DMEM_SIZE];
} fmiss_vm;

/* Callbacks into the owning NAND controller's register space, keeping the
 * VM decoupled from S5L8702NandState. */
typedef struct FmissNandOps {
    uint64_t (*read)(void *opaque, hwaddr addr, unsigned size);
    void     (*write)(void *opaque, hwaddr addr, uint64_t val, unsigned size);
} FmissNandOps;

void fmiss_vm_reset(fmiss_vm *vm, uint32_t pc);
void fmiss_vm_execute(const FmissNandOps *ops, void *opaque, fmiss_vm *vm);

#endif /* HW_MISC_S5L8702_NAND_FMISS_H */

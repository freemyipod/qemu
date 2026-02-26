#ifndef HW_MISC_S5L8702_NAND_ECC_H
#define HW_MISC_S5L8702_NAND_ECC_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"

/* Register offsets */
#define NANDECC_DATA        0x4
#define NANDECC_ECC         0x8
#define NANDECC_START       0xC
#define NANDECC_STATUS      0x10
#define NANDECC_SETUP       0x14
#define NANDECC_CLEARINT    0x40

#define S5L8702_NAND_ECC_BASE   0x38A02000
#define S5L8702_NAND_ECC_SIZE   0x100

#define S5L8702_NAND_ECC_IRQ 43

#define TYPE_S5L8702_NAND_ECC   "s5l8702-nand-ecc"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702NandEccState, S5L8702_NAND_ECC)

struct S5L8702NandEccState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t data_addr;
    uint32_t ecc_addr;
    uint32_t status;
    uint32_t setup;
};

#endif /* HW_MISC_S5L8702_NAND_ECC_H */

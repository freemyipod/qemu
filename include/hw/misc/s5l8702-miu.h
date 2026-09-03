#ifndef HW_MISC_S5L8702_MIU_H
#define HW_MISC_S5L8702_MIU_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/misc/s5l8702-buscon.h"

#define TYPE_S5L8702_MIU    "s5l8702-miu"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702MiuState, S5L8702_MIU)

#define S5L8702_MIU_BASE    0x38100000
#define S5L8702_MIU_SIZE    0x00100000

#define S5L8702_MIU_NUM_REGS    (S5L8702_MIU_SIZE / sizeof(uint32_t))

struct S5L8702MiuState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    uint32_t regs[S5L8702_MIU_NUM_REGS];

    /* Owned by the SoC; MIUCON's remap bit drives it. */
    S5L8702BusConState *buscon;
};

#endif /* HW_MISC_S5L8702_MIU_H */

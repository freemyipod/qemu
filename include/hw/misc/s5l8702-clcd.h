#ifndef HW_MISC_S5L8702_CLCD_H
#define HW_MISC_S5L8702_CLCD_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "exec/memory.h"

#define TYPE_S5L8702_CLCD "s5l8702-clcd"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702ClcdState, S5L8702_CLCD)

#define S5L8702_CLCD_BASE   0x38900000
#define S5L8702_CLCD_SIZE   0x00001000

#define S5L8702_CLCD_WINDOWS 5

struct S5L8702ClcdState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    uint32_t regs[S5L8702_CLCD_SIZE / sizeof(uint32_t)];
    AddressSpace *as;
};

bool s5l8702_clcd_enabled(S5L8702ClcdState *s);

void s5l8702_clcd_composite(S5L8702ClcdState *s, uint32_t *dest, int width, int height);

#endif /* HW_MISC_S5L8702_CLCD_H */

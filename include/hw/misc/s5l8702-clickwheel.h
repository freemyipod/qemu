#ifndef HW_MISC_S5L8702_CLICKWHEEL_H
#define HW_MISC_S5L8702_CLICKWHEEL_H

#include "qom/object.h"
#include "hw/sysbus.h"

#define TYPE_S5L8702_CLICKWHEEL    "s5l8702-clickwheel"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702ClickwheelState, S5L8702_CLICKWHEEL)

#define S5L8702_CLICKWHEEL_BASE    0x3C200000
#define S5L8702_CLICKWHEEL_SIZE    0x00000100

struct S5L8702ClickwheelState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
};

#endif /* HW_MISC_S5L8702_CLICKWHEEL_H */

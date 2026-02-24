#ifndef HW_MISC_S5L8702_CHIPID_H
#define HW_MISC_S5L8702_CHIPID_H

#include "qom/object.h"
#include "hw/sysbus.h"

#define TYPE_S5L8702_CHIPID "s5l8702-chipid"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702ChipIDState, S5L8702_CHIPID)

#define S5L8702_CHIPID_BASE 0x3D100000
#define S5L8702_CHIPID_SIZE 0x00100000

#define S5L8702_CHIP_REVISION 0x0

struct S5L8702ChipIDState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
};

#endif /* HW_MISC_S5L8702_CHIPID_H */

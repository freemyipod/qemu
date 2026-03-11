#ifndef HW_MISC_S5L8702_USBPHY_H
#define HW_MISC_S5L8702_USBPHY_H

#include "qom/object.h"
#include "hw/sysbus.h"

#define TYPE_S5L8702_USBPHY    "s5l8702-usbphy"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702UsbPhyState, S5L8702_USBPHY)

#define S5L8702_USBPHY_BASE    0x3C400000
#define S5L8702_USBPHY_SIZE    0x00100000

#define S5L8702_USBPHY_NUM_REGS    (S5L8702_USBPHY_SIZE / sizeof(uint32_t))

struct S5L8702UsbPhyState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    uint32_t regs[S5L8702_USBPHY_NUM_REGS];
};

#endif /* HW_MISC_S5L8702_USBPHY_H */

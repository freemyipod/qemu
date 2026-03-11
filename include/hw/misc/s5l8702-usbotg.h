#ifndef HW_MISC_S5L8702_USBOTG_H
#define HW_MISC_S5L8702_USBOTG_H

#include "qom/object.h"
#include "hw/sysbus.h"

#define TYPE_S5L8702_USBOTG    "s5l8702-usbotg"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702UsbOtgState, S5L8702_USBOTG)

#define S5L8702_USBOTG_BASE    0x38400000
#define S5L8702_USBOTG_SIZE    0x00001000

#define S5L8702_USBOTG_NUM_REGS    (S5L8702_USBOTG_SIZE / sizeof(uint32_t))

struct S5L8702UsbOtgState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    uint32_t regs[S5L8702_USBOTG_NUM_REGS];
};

#endif /* HW_MISC_S5L8702_USBOTG_H */

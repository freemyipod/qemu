#ifndef HW_MISC_S5L8702_SYSIC_H
#define HW_MISC_S5L8702_SYSIC_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_S5L8702_SYSIC "s5l8702-sysic"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702SysICState, S5L8702_SYSIC)

#define S5L8702_SYSIC_BASE   0x39a00000
#define S5L8702_SYSIC_SIZE   0x00100000
#define S5L8702_SYSIC_GPIO_GROUPS 7

/* USB detection uses GPIO group 5 (pin ~26) */
#define S5L8702_SYSIC_USB_GPIO_GROUP 5
#define S5L8702_SYSIC_USB_GPIO_PIN   26

#define S5L8702_SYSIC_GPIO_IRQ_BASE 9
#define S5L8702_SYSIC_GPIO_IRQ(grp) (S5L8702_SYSIC_GPIO_IRQ_BASE + (grp))

struct S5L8702SysICState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    qemu_irq gpio_irqs[S5L8702_SYSIC_GPIO_GROUPS];

    uint32_t power_state;

    uint32_t gpio_int_level[S5L8702_SYSIC_GPIO_GROUPS];
    uint32_t gpio_int_status[S5L8702_SYSIC_GPIO_GROUPS];
    uint32_t gpio_int_enabled[S5L8702_SYSIC_GPIO_GROUPS];
    uint32_t gpio_int_type[S5L8702_SYSIC_GPIO_GROUPS];

    /* USB detection state */
    bool usb_connected;
};

#endif
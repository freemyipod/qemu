#ifndef HW_I2C_S5L8702_I2C_H
#define HW_I2C_S5L8702_I2C_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/i2c/i2c.h"

#define TYPE_S5L8702_I2C    "s5l8702-i2c"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702I2cState, S5L8702_I2C)

#define S5L8702_I2C0_BASE   0x3C600000
#define S5L8702_I2C1_BASE   0x3C610000
#define S5L8702_I2C_SIZE    0x00100000

#define S5L8702_I2C0_IRQ    21
#define S5L8702_I2C1_IRQ    22

#define S5L8702_I2C_NUM_REGS    (S5L8702_I2C_SIZE / sizeof(uint32_t))

struct S5L8702I2cState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    I2CBus *bus;
    qemu_irq irq;
    QEMUTimer *irq_timer;
    bool last_was_start;

    uint32_t iiccon;
    uint32_t iicstat;
    uint32_t iicadd;
    uint32_t iicds;
    uint32_t iicunk10;
    uint32_t iicunk14;
    uint32_t iicunk18;
    uint32_t iicstat2;

    uint8_t rx_shift_register;
};

#endif /* HW_I2C_S5L8702_I2C_H */

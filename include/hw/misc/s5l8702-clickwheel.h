#ifndef HW_MISC_S5L8702_CLICKWHEEL_H
#define HW_MISC_S5L8702_CLICKWHEEL_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "qemu/timer.h"
#include "hw/gpio/s5l8702-gpio.h"

#define TYPE_S5L8702_CLICKWHEEL    "s5l8702-clickwheel"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702ClickwheelState, S5L8702_CLICKWHEEL)

#define S5L8702_CLICKWHEEL_BASE    0x3C200000
#define S5L8702_CLICKWHEEL_SIZE    0x00000100

#define S5L8702_CWHEEL_IRQ_GLUE     23

/*
 * Register offsets (from Rockbox button-clickwheel.c / s5l8702 headers)
 *
 * WHEEL00  0x00  - Control: 0=stop, non-zero=start (e.g. 0x380000)
 * WHEEL04  0x04  - Enable: bit 0 = enable controller
 * WHEEL08  0x08  - Timing register (e.g. 0x20000)
 * WHEEL0C  0x0C  - Unknown
 * WHEEL10  0x10  - Config register (e.g. 1)
 * WHEELINT 0x14  - Interrupt status (write to clear; bit0/1/2 = event flags)
 * WHEELRX  0x18  - Received data from clickwheel
 * WHEELTX  0x1C  - Transmit data to clickwheel (init cmd = 0x8000023A)
 */

struct S5L8702ClickwheelState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    /* VIC interrupt output */
    qemu_irq irq;

    /* Reference to GPIO state (set by parent SoC) for reading button state */
    S5L8702GpioState *gpio;

    /* Registers */
    uint32_t reg_control;   /* WHEEL00 */
    uint32_t reg_enable;    /* WHEEL04 */
    uint32_t reg_timing;    /* WHEEL08 */
    uint32_t reg_unk0c;     /* WHEEL0C */
    uint32_t reg_config;    /* WHEEL10 */
    uint32_t reg_int;       /* WHEELINT */
    uint32_t reg_rx;        /* WHEELRX */
    uint32_t reg_tx;        /* WHEELTX */

    /* Internal state */
    bool enabled;
    bool init_sent;         /* WHEELTX received init command (0x8000023A) */
    QEMUTimer *init_timer;  /* one-shot timer for init response */
};

#endif /* HW_MISC_S5L8702_CLICKWHEEL_H */

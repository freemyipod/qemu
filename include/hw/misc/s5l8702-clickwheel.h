#ifndef HW_MISC_S5L8702_CLICKWHEEL_H
#define HW_MISC_S5L8702_CLICKWHEEL_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "qemu/timer.h"
#include "hw/gpio/s5l8702-gpio.h"
#include "ui/input.h"

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
 * WHEEL0C  0x0C  - Interrupt status, read-only: bit 0 = a packet is waiting in WHEELRX.
 * WHEEL10  0x10  - Interrupt enable: bit 0 = deliver interrupts
 * WHEELINT 0x14  - Interrupt acknowledge (write to clear; bit0/1/2 = events).
 * WHEELRX  0x18  - Received data from clickwheel
 * WHEELTX  0x1C  - Transmit data to clickwheel
 *
 * WHEELRX Packet Word:
 *   init mode     (value & 0x8000FFFF) == 0x8000023A
 *                 buttons in bits [20:16]
 *   normal mode   (value & 0xBC0000FF) == 0x8000001A
 *                 buttons in bits [12:8]
 *                 bit 30      = a finger is on the wheel
 *                 bits [25:16] = wheel position, 0..95 (96 per revolution).
 *                 0x60 modulus, which is where the 96 comes from.
 */

#define S5L8702_CLICKWHEEL_POSITIONS 96

struct S5L8702ClickwheelState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    /* VIC interrupt output */
    qemu_irq irq;

    /* Reference to GPIO state (set by parent SoC) for reading button state */
    S5L8702GpioState *gpio;

    uint32_t reg_control;   /* WHEEL00 */
    uint32_t reg_enable;    /* WHEEL04 */
    uint32_t reg_timing;    /* WHEEL08 */
    uint32_t reg_status;    /* WHEEL0C: bit 0 = a packet is waiting in WHEELRX */
    uint32_t reg_config;    /* WHEEL10 */
    uint32_t reg_int;       /* WHEELINT */
    uint32_t reg_rx;        /* WHEELRX */
    uint32_t reg_tx;        /* WHEELTX */

    bool enabled;
    bool init_sent;         // WHEELTX received init command (0x8000023A)
    QEMUTimer *init_timer;  // one-shot timer for init response
    uint32_t pending_cmd;   // command decoded from WHEELTX, awaiting the WHEEL04 start bit

    QemuInputHandlerState *input;
    QEMUTimer *release_timer; // lifts the "finger" after the scrolling stops
    QEMUTimer *report_timer;  // re-sends the packet while a button is held
    uint32_t wheel_pos;       // 0..S5L8702_CLICKWHEEL_POSITIONS-1
    bool wheel_touched;       // a finger is on the wheel right now

    uint32_t scroll_step;     // wheel positions per host scroll click
    uint32_t release_ms;      // idle time before the finger lifts
    uint32_t report_ms;       // packet-to-packet interval while a button is held
};

#endif /* HW_MISC_S5L8702_CLICKWHEEL_H */

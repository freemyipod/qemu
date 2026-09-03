#ifndef HW_TIMER_S5L8702_TIMER_H
#define HW_TIMER_S5L8702_TIMER_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "hw/clock.h"
#include "hw/irq.h"

#define TYPE_S5L8702_TIMER  "s5l8702-timer"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702TimerCtrlState, S5L8702_TIMER)

#define S5L8702_TIMER_BASE  0x3C700000
#define S5L8702_TIMER_SIZE  0x00100000

/* VIC0 interrupt lines used by the timer controller */
#define S5L8702_TIMER_IRQ_32BIT  7  /* timers 4-7 (32-bit) */
#define S5L8702_TIMER_IRQ_16BIT  8  /* timers 0-3 (16-bit) */

#define S5L8702_TIMER_COUNT_16  4
#define S5L8702_TIMER_COUNT_32  4
#define S5L8702_TIMER_COUNT     (S5L8702_TIMER_COUNT_16 + S5L8702_TIMER_COUNT_32)

typedef enum {
    S5L8702_TIMER_TYPE_16,
    S5L8702_TIMER_TYPE_32,
} S5L8702TimerType;

typedef struct S5L8702Timer {
    S5L8702TimerCtrlState *ctrl;
    S5L8702TimerType type;
    QEMUTimer timer;

    /* Per-timer registers */
    uint32_t tcon;
    uint32_t tcmd;
    uint32_t tdata0;
    uint32_t tdata1;
    uint32_t tpre;
    uint32_t tcnt;

    /* Runtime timing state */
    bool     running;       /* timer is currently counting */
    uint64_t start_ns;      /* QEMU virtual clock (ns) when timer started/reset */
    uint32_t start_count;   /* counter value at start_ns */
    uint32_t sched_count;   /* counter value scheduled for next QEMUTimer expiry */
    uint32_t sched_events;  /* bitmask of events due at sched_count */
} S5L8702Timer;

struct S5L8702TimerCtrlState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    /*
     * "interval-reload": restart an interval-mode counter from 0 at its TDATA0
     * compare, which is what the hardware does. On by default -- it is what
     * turns the 32-bit timer at 0xC0 into the 100 Hz system tick the firmware
     * programs it as; without it the counter only comes back round on the
     * full-width wrap, one interrupt every 4295 seconds, and the firmware
     * never gets a tick at all. Kept as a knob so a boot can be compared with
     * and without it.
     */
    bool interval_reload;

    Clock *pclk;
    Clock *eclk;
    Clock *extclk0;
    Clock *extclk1;
    qemu_irq irq_16bit;  /* shared IRQ for timers 0-3 */
    qemu_irq irq_32bit;  /* shared IRQ for timers 4-7 */
    S5L8702Timer timer[S5L8702_TIMER_COUNT];
    uint32_t tstat;
};

#endif /* HW_TIMER_S5L8702_TIMER_H */

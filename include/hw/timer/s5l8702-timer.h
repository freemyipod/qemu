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
     * compare, which is what the hardware does. Default false, because with it
     * on the machine dies within a second of boot, and the reason is a second
     * defect in this model rather than in the reload itself.
     *
     * What happens: the reload delivers the 100 Hz tick the firmware programs
     * on the 32-bit timer at 0xC0, and the firmware's handler answers it by
     * writing the free-running microsecond counter to TSTAT (0x118) once per
     * tick -- not a status mask. This model treats the 32-bit timer IRQ as a
     * level held until the matching TSTAT bit is cleared, so those writes only
     * happen to clear timer F's bit (1 << 16) while the microsecond counter is
     * in [0x10000, 0x20000), i.e. for the first ~131 ms. After that the line
     * stays asserted, the handler re-enters every ~4 us, and the boot ends in
     * the bootrom's halt loop at 0x20001348.
     *
     * So the 32-bit timers' acknowledge path is modelled wrongly, and that has
     * to be settled before this can be turned on by default.
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

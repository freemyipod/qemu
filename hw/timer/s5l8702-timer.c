#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/clock.h"
#include "qemu/module.h"
#include "hw/timer/s5l8702-timer.h"
#include "trace.h"

#define SCHED_EVT_INT0  BIT(0)
#define SCHED_EVT_INT1  BIT(1)
#define SCHED_EVT_OVF   BIT(2)

// 16-bit timer registers
#define S5L8702_TIMER_TCON_16(x)    ((x) * 0x20 + 0x00)
#define S5L8702_TIMER_TCMD_16(x)    ((x) * 0x20 + 0x04)
#define S5L8702_TIMER_TDATA0_16(x)  ((x) * 0x20 + 0x08)
#define S5L8702_TIMER_TDATA1_16(x)  ((x) * 0x20 + 0x0C)
#define S5L8702_TIMER_TPRE_16(x)    ((x) * 0x20 + 0x10)
#define S5L8702_TIMER_TCNT_16(x)    ((x) * 0x20 + 0x14)

// 32-bit timer registers (offset by 0x20)
#define S5L8702_TIMER_TCON_32(x)    ((x) * 0x20 + 0x20 + 0x00)
#define S5L8702_TIMER_TCMD_32(x)    ((x) * 0x20 + 0x20 + 0x04)
#define S5L8702_TIMER_TDATA0_32(x)  ((x) * 0x20 + 0x20 + 0x08)
#define S5L8702_TIMER_TDATA1_32(x)  ((x) * 0x20 + 0x20 + 0x0C)
#define S5L8702_TIMER_TPRE_32(x)    ((x) * 0x20 + 0x20 + 0x10)
#define S5L8702_TIMER_TCNT_32(x)    ((x) * 0x20 + 0x20 + 0x14)

// TCON register
#define S5L8702_TIMER_TCON_OUT      BIT(20)
#define S5L8702_TIMER_TCON_OVF      BIT(18)
#define S5L8702_TIMER_TCON_INT1     BIT(17)
#define S5L8702_TIMER_TCON_INT0     BIT(16)
#define S5L8702_TIMER_TCON_OVF_EN   BIT(14)
#define S5L8702_TIMER_TCON_INT1_EN  BIT(13)
#define S5L8702_TIMER_TCON_INT0_EN  BIT(12)
#define S5L8702_TIMER_TCON_START    BIT(11)
#define S5L8702_TIMER_TCON_CS(x)            (((x) & 0x7) << 8)
#define S5L8702_TIMER_TCON_CS_MASK          (0x7 << 8)
#define S5L8702_TIMER_TCON_CAP_MODE BIT(7)
#define S5L8702_TIMER_TCON_ECLK     BIT(6)
#define S5L8702_TIMER_TCON_MODE_SEL(x)      (((x) & 0x3) << 4)
#define S5L8702_TIMER_TCON_MODE_SEL_MASK    (0x3 << 4)

// TCMD register
#define S5L8702_TIMER_TCMD_CLR      BIT(1)
#define S5L8702_TIMER_TCMD_EN       BIT(0)

// TSTAT register
#define S5L8702_TIMER_TSTAT_INTE    BIT(24)
#define S5L8702_TIMER_TSTAT_INTF    BIT(16)
#define S5L8702_TIMER_TSTAT_INTG    BIT(8)
#define S5L8702_TIMER_TSTAT_INTH    BIT(0)

// Global timer registers
#define S5L8702_TIMER_TSTAT         0x118

static void s5l8702_timer_schedule(S5L8702Timer *t);

static uint32_t s5l8702_timer_max_val(S5L8702Timer *t) {
    return (t->type == S5L8702_TIMER_TYPE_16) ? 0xFFFF : 0xFFFFFFFF;
}

static uint64_t s5l8702_timer_get_freq(S5L8702Timer *t) {
    S5L8702TimerCtrlState *s = t->ctrl;
    uint32_t prescale = (t->tpre & 0x3FF) + 1;
    uint32_t div;
    Clock *clk;

    if (t->tcon & S5L8702_TIMER_TCON_ECLK) clk = s->eclk;
    else clk = s->pclk;

    switch (t->tcon & S5L8702_TIMER_TCON_CS_MASK) {
        case S5L8702_TIMER_TCON_CS(0): div = 2; break;
        case S5L8702_TIMER_TCON_CS(1): div = 4; break;
        case S5L8702_TIMER_TCON_CS(2): div = 16; break;
        case S5L8702_TIMER_TCON_CS(3): div = 64; break;
        case S5L8702_TIMER_TCON_CS(4):
        case S5L8702_TIMER_TCON_CS(5): clk = s->extclk0; div = 1; break;
        case S5L8702_TIMER_TCON_CS(6):
        case S5L8702_TIMER_TCON_CS(7): clk = s->extclk1; div = 1; break;
        default: div = 1; break;
    }

    uint64_t base = clock_get_hz(clk);
    uint64_t denom = (uint64_t)div * prescale;
    if (base == 0 || denom == 0) return 0;
    return base / denom;
}

static uint32_t s5l8702_timer_current_count(S5L8702Timer *t) {
    if (!t->running) return t->tcnt;

    uint64_t freq = s5l8702_timer_get_freq(t);
    if (freq == 0) return t->tcnt;

    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t elapsed_ns = now - t->start_ns;
    uint64_t elapsed_ticks = muldiv64(elapsed_ns, freq, NANOSECONDS_PER_SECOND);
    uint32_t max_val = s5l8702_timer_max_val(t);

    return (uint32_t)((t->start_count + elapsed_ticks) & (uint64_t)max_val);
}

static uint64_t s5l8702_dist_to_count(uint32_t cur, uint32_t target, uint32_t max_val) {
    if (target > cur)  return target - cur;
    /* target <= cur: must wrap around */
    return (uint64_t)(max_val - cur) + 1 + target;
}

static void s5l8702_timer_update(S5L8702Timer *t) {
    S5L8702TimerCtrlState *s = t->ctrl;
    bool is_32bit = (t->type == S5L8702_TIMER_TYPE_32);
    uint32_t start = is_32bit ? S5L8702_TIMER_COUNT_16 : 0;
    uint32_t end = is_32bit ? S5L8702_TIMER_COUNT : S5L8702_TIMER_COUNT_16;

    bool irq = false;
    for (uint32_t i = start; i < end; i++) {
        S5L8702Timer *ti = &s->timer[i];
        
        if (is_32bit) {
            /* 32-bit timers assert based on TSTAT bits, provided they are enabled in TCON */
            uint32_t shift = (i == 4) ? 8 : (i == 5) ? 16 : (i == 6) ? 24 : 0;
            if ((s->tstat & (4 << shift)) && (ti->tcon & S5L8702_TIMER_TCON_OVF_EN)) irq = true;
            if ((s->tstat & (1 << shift)) && (ti->tcon & S5L8702_TIMER_TCON_INT0_EN)) irq = true;
            if ((s->tstat & (2 << shift)) && (ti->tcon & S5L8702_TIMER_TCON_INT1_EN)) irq = true;
        } else {
            /* 16-bit timers assert directly from TCON */
            if (((ti->tcon & S5L8702_TIMER_TCON_OVF)  && (ti->tcon & S5L8702_TIMER_TCON_OVF_EN)) ||
                ((ti->tcon & S5L8702_TIMER_TCON_INT1) && (ti->tcon & S5L8702_TIMER_TCON_INT1_EN)) ||
                ((ti->tcon & S5L8702_TIMER_TCON_INT0) && (ti->tcon & S5L8702_TIMER_TCON_INT0_EN))) {
                irq = true;
            }
        }
        if (irq) break;
    }

    qemu_set_irq(is_32bit ? s->irq_32bit : s->irq_16bit, irq);
}

/*
 * Set the TSTAT status bit for 32-bit timers when an interrupt fires.
 * TSTAT bits are only set here (on fire), and cleared by the guest.
 */
static void s5l8702_timer_set_tstat(S5L8702Timer *t) {
    S5L8702TimerCtrlState *s = t->ctrl;
    uint32_t idx = (uint32_t)(t - &s->timer[0]);
    
    if (idx >= S5L8702_TIMER_COUNT_16 && idx < S5L8702_TIMER_COUNT) {
        /* Map timer 4->shift 8, 5->16, 6->24, 7->0 */
        uint32_t shift = (idx == 4) ? 8 : (idx == 5) ? 16 : (idx == 6) ? 24 : 0;
        
        if (t->sched_events & SCHED_EVT_INT0) s->tstat |= (1 << shift);
        if (t->sched_events & SCHED_EVT_INT1) s->tstat |= (2 << shift);
        if (t->sched_events & SCHED_EVT_OVF)  s->tstat |= (4 << shift);
    }
}

static void s5l8702_timer_clk_select(S5L8702Timer *t, uint32_t tcon, uint32_t tpre) {
    if (tcon & S5L8702_TIMER_TCON_ECLK) {
        switch (tcon & S5L8702_TIMER_TCON_CS_MASK) {
        case S5L8702_TIMER_TCON_CS(0):
            trace_s5l8702_timer_clk_select("ECLK / 2");
            break;
        case S5L8702_TIMER_TCON_CS(1):
            trace_s5l8702_timer_clk_select("ECLK / 4");
            break;
        case S5L8702_TIMER_TCON_CS(2):
            trace_s5l8702_timer_clk_select("ECLK / 16");
            break;
        case S5L8702_TIMER_TCON_CS(3):
            trace_s5l8702_timer_clk_select("ECLK / 64");
            break;
        case S5L8702_TIMER_TCON_CS(4):
        case S5L8702_TIMER_TCON_CS(5):
            trace_s5l8702_timer_clk_select("external clock 0");
            break;
        case S5L8702_TIMER_TCON_CS(6):
        case S5L8702_TIMER_TCON_CS(7):
            trace_s5l8702_timer_clk_select("external clock 1");
            break;
        default:
            trace_s5l8702_timer_clk_select_invalid(((uint32_t) tcon & S5L8702_TIMER_TCON_CS_MASK) >> 8);
        }
    } else {
        switch (tcon & S5L8702_TIMER_TCON_CS_MASK) {
        case S5L8702_TIMER_TCON_CS(0):
            trace_s5l8702_timer_clk_select("PCLK / 2");
            break;
        case S5L8702_TIMER_TCON_CS(1):
            trace_s5l8702_timer_clk_select("PCLK / 4");
            break;
        case S5L8702_TIMER_TCON_CS(2):
            trace_s5l8702_timer_clk_select("PCLK / 16");
            break;
        case S5L8702_TIMER_TCON_CS(3):
            trace_s5l8702_timer_clk_select("PCLK / 64");
            break;
        case S5L8702_TIMER_TCON_CS(4):
        case S5L8702_TIMER_TCON_CS(5):
            trace_s5l8702_timer_clk_select("external clock 0");
            break;
        case S5L8702_TIMER_TCON_CS(6):
        case S5L8702_TIMER_TCON_CS(7):
            trace_s5l8702_timer_clk_select("external clock 1");
            break;
        default:
            trace_s5l8702_timer_clk_select_invalid(((uint32_t) tcon & S5L8702_TIMER_TCON_CS_MASK) >> 8);
        }
    }

    /* If the timer is running, reschedule with the updated frequency */
    if (t->running) s5l8702_timer_schedule(t);
}

static void s5l8702_timer_mode_select(S5L8702Timer *t, uint32_t tcon) {
    switch (tcon & S5L8702_TIMER_TCON_MODE_SEL_MASK) {
    case S5L8702_TIMER_TCON_MODE_SEL(0): // Interval mode
        trace_s5l8702_timer_mode_select("interval");
        break;
    case S5L8702_TIMER_TCON_MODE_SEL(1): // PWM mode
        trace_s5l8702_timer_mode_select("PWM");
        break;
    case S5L8702_TIMER_TCON_MODE_SEL(2): // One-shot mode
        trace_s5l8702_timer_mode_select("one-shot");
        break;
    case S5L8702_TIMER_TCON_MODE_SEL(3): // Capture mode
        trace_s5l8702_timer_mode_select("capture");
        break;
    }
}

static void s5l8702_timer_clear(S5L8702Timer *t) {
    trace_s5l8702_timer_clear();

    /* Reset counter to 0 and update the timing reference */
    t->tcnt = 0;
    t->start_count = 0;
    t->start_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (t->running) s5l8702_timer_schedule(t);
}

static void s5l8702_timer_enable(S5L8702Timer *t, uint32_t tcmd) {
    bool enable = !!(tcmd & S5L8702_TIMER_TCMD_EN);
    trace_s5l8702_timer_enable(enable);

    if (enable && !t->running) {
        /* Start: resume counting from last known count */
        t->running = true;
        t->start_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        t->start_count = t->tcnt;
        s5l8702_timer_schedule(t);
    } else if (!enable && t->running) {
        /* Stop: snapshot current count and cancel the timer */
        t->tcnt = s5l8702_timer_current_count(t);
        t->running = false;
        timer_del(&t->timer);
    }
}

static uint32_t s5l8702_timer_get_cnt(S5L8702Timer *t) {
    if (t->running) t->tcnt = s5l8702_timer_current_count(t);
    return t->tcnt;
}

/*
 * Free-running 1us counter exposed in the timer MMIO region at offset
 * 0x10000. The Apple OF bootloader reads this to timestamp interrupt
 * arrivals and to drive its software timeouts; if it never advances the
 * boot stalls in an IRQ storm because no scheduled callback ever fires.
 */
#define S5L8702_TIMER_USEC          0x10000

static uint64_t s5l8702_timer_read(void *opaque, hwaddr offset, unsigned size) {
    S5L8702TimerCtrlState *s = S5L8702_TIMER(opaque);
    uint32_t tidx = offset / 0x20;
    if (tidx >= S5L8702_TIMER_COUNT_16 + 1) tidx--; /* skip 0x80..0x9F gap before 32-bit block */
    S5L8702Timer *t = &s->timer[tidx];
    uint32_t r = 0;
    bool implemented = true;

    if (offset == S5L8702_TIMER_USEC) {
        return (uint32_t)(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 1000ULL);
    }

    switch (offset) {
    case S5L8702_TIMER_TCON_16(0):
    case S5L8702_TIMER_TCON_16(1):
    case S5L8702_TIMER_TCON_16(2):
    case S5L8702_TIMER_TCON_16(3):
    case S5L8702_TIMER_TCON_32(4):
    case S5L8702_TIMER_TCON_32(5):
    case S5L8702_TIMER_TCON_32(6):
    case S5L8702_TIMER_TCON_32(7): {
        r = t->tcon;
        trace_s5l8702_timer_read("tcon", tidx, r);
        break;
    }
    case S5L8702_TIMER_TCMD_16(0):
    case S5L8702_TIMER_TCMD_16(1):
    case S5L8702_TIMER_TCMD_16(2):
    case S5L8702_TIMER_TCMD_16(3):
    case S5L8702_TIMER_TCMD_32(4):
    case S5L8702_TIMER_TCMD_32(5):
    case S5L8702_TIMER_TCMD_32(6):
    case S5L8702_TIMER_TCMD_32(7): {
        r = t->tcmd;
        trace_s5l8702_timer_read("tcmd", tidx, r);
        break;
    }
    case S5L8702_TIMER_TDATA0_16(0):
    case S5L8702_TIMER_TDATA0_16(1):
    case S5L8702_TIMER_TDATA0_16(2):
    case S5L8702_TIMER_TDATA0_16(3):
    case S5L8702_TIMER_TDATA0_32(4):
    case S5L8702_TIMER_TDATA0_32(5):
    case S5L8702_TIMER_TDATA0_32(6):
    case S5L8702_TIMER_TDATA0_32(7): {
        r = t->tdata0;
        trace_s5l8702_timer_read("tdata0", tidx, r);
        break;
    }
    case S5L8702_TIMER_TDATA1_16(0):
    case S5L8702_TIMER_TDATA1_16(1):
    case S5L8702_TIMER_TDATA1_16(2):
    case S5L8702_TIMER_TDATA1_16(3):
    case S5L8702_TIMER_TDATA1_32(4):
    case S5L8702_TIMER_TDATA1_32(5):
    case S5L8702_TIMER_TDATA1_32(6):
    case S5L8702_TIMER_TDATA1_32(7): {
        r = t->tdata1;
        trace_s5l8702_timer_read("tdata1", tidx, r);
        break;
    }
    case S5L8702_TIMER_TPRE_16(0):
    case S5L8702_TIMER_TPRE_16(1):
    case S5L8702_TIMER_TPRE_16(2):
    case S5L8702_TIMER_TPRE_16(3):
    case S5L8702_TIMER_TPRE_32(4):
    case S5L8702_TIMER_TPRE_32(5):
    case S5L8702_TIMER_TPRE_32(6):
    case S5L8702_TIMER_TPRE_32(7): {
        r = t->tpre;
        trace_s5l8702_timer_read("tpre", tidx, r);
        break;
    }
    case S5L8702_TIMER_TCNT_16(0):
    case S5L8702_TIMER_TCNT_16(1):
    case S5L8702_TIMER_TCNT_16(2):
    case S5L8702_TIMER_TCNT_16(3):
    case S5L8702_TIMER_TCNT_32(4):
    case S5L8702_TIMER_TCNT_32(5):
    case S5L8702_TIMER_TCNT_32(6):
    case S5L8702_TIMER_TCNT_32(7): {
        r = s5l8702_timer_get_cnt(t);
        trace_s5l8702_timer_read("tcnt", tidx, r);
        break;
    }
    case S5L8702_TIMER_TSTAT: {
        r = s->tstat;
        trace_s5l8702_timer_read("tstat", 0, r);
        break;
    }
    default:
        trace_s5l8702_timer_read_unimp((uint32_t) offset);
        implemented = false;
    }

    if(implemented) s5l8702_timer_update(t);

    return r;
}

static void s5l8702_timer_write(void *opaque, hwaddr offset, uint64_t val, unsigned size) {
    S5L8702TimerCtrlState *s = S5L8702_TIMER(opaque);
    uint32_t tidx = offset / 0x20;
    if (tidx >= S5L8702_TIMER_COUNT_16 + 1) tidx--; /* skip 0x80..0x9F gap before 32-bit block */
    S5L8702Timer *t = &s->timer[tidx];

    switch (offset) {
    case S5L8702_TIMER_TCON_16(0):
    case S5L8702_TIMER_TCON_16(1):
    case S5L8702_TIMER_TCON_16(2):
    case S5L8702_TIMER_TCON_16(3):
    case S5L8702_TIMER_TCON_32(4):
    case S5L8702_TIMER_TCON_32(5):
    case S5L8702_TIMER_TCON_32(6):
    case S5L8702_TIMER_TCON_32(7): {
        trace_s5l8702_timer_write("tcon", tidx, (uint32_t) val);
        if (!t->running) {
            trace_s5l8702_timer_clear(); // Log that we are doing this.
            t->tcnt = 0;
            t->start_count = 0;
            t->start_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        }

        /*
         * Correctly model the mixed-mode register:
         * - Status flags (OVF, INT1, INT0) are Write-1-to-Clear (W1C).
         * - Other bits (enables, mode, etc.) are Read/Write (R/W).
         * - The output bit is Read-Only (R/O).
         */
        uint32_t w1c_mask = S5L8702_TIMER_TCON_OVF |
                            S5L8702_TIMER_TCON_INT1 |
                            S5L8702_TIMER_TCON_INT0;

        uint32_t rw_mask = ~w1c_mask & ~S5L8702_TIMER_TCON_OUT;

        /* Start with the current register state */
        uint32_t new_tcon = t->tcon;

        /* Apply the R/W bits from the guest's write */
        new_tcon &= ~rw_mask;      /* Clear the R/W bits in our state */
        new_tcon |= val & rw_mask; /* Apply the new R/W bits from val */

        /* Clear the status bits that the guest wrote a '1' to */
        new_tcon &= ~(val & w1c_mask);

        /*
         * For 32-bit timers the IRQ line is driven from TSTAT, not TCON.
         * Mirror the TCON W1C clears into the corresponding TSTAT bits so
         * that the IRQ actually goes low when the guest acknowledges via
         * TCON (which the OF firmware does instead of writing TSTAT).
         */
        if (t->type == S5L8702_TIMER_TYPE_32) {
            uint32_t idx   = tidx;
            uint32_t shift = (idx == 4) ? 8 : (idx == 5) ? 16 : (idx == 6) ? 24 : 0;
            if (val & S5L8702_TIMER_TCON_INT0) s->tstat &= ~(1u << shift);
            if (val & S5L8702_TIMER_TCON_INT1) s->tstat &= ~(2u << shift);
            if (val & S5L8702_TIMER_TCON_OVF)  s->tstat &= ~(4u << shift);
        }

        /* Check if clock source or mode changed to reschedule */
        if ((new_tcon & S5L8702_TIMER_TCON_CS_MASK) != (t->tcon & S5L8702_TIMER_TCON_CS_MASK) ||
            (new_tcon & S5L8702_TIMER_TCON_MODE_SEL_MASK) != (t->tcon & S5L8702_TIMER_TCON_MODE_SEL_MASK)) {
            s5l8702_timer_clk_select(t, new_tcon, t->tpre);
            s5l8702_timer_mode_select(t, new_tcon);
        }

        t->tcon = new_tcon;
        break;
    }
    case S5L8702_TIMER_TCMD_16(0):
    case S5L8702_TIMER_TCMD_16(1):
    case S5L8702_TIMER_TCMD_16(2):
    case S5L8702_TIMER_TCMD_16(3):
    case S5L8702_TIMER_TCMD_32(4):
    case S5L8702_TIMER_TCMD_32(5):
    case S5L8702_TIMER_TCMD_32(6):
    case S5L8702_TIMER_TCMD_32(7): {
        if (val & S5L8702_TIMER_TCMD_CLR) {
            val &= ~S5L8702_TIMER_TCMD_CLR;
            s5l8702_timer_clear(t);
        }

        s5l8702_timer_enable(t, val);

        t->tcmd = (uint32_t) val;
        break;
    }
    case S5L8702_TIMER_TDATA0_16(0):
    case S5L8702_TIMER_TDATA0_16(1):
    case S5L8702_TIMER_TDATA0_16(2):
    case S5L8702_TIMER_TDATA0_16(3):
    case S5L8702_TIMER_TDATA0_32(4):
    case S5L8702_TIMER_TDATA0_32(5):
    case S5L8702_TIMER_TDATA0_32(6):
    case S5L8702_TIMER_TDATA0_32(7): {
        trace_s5l8702_timer_write("tdata0", tidx, (uint32_t) val);
        t->tdata0 = (uint32_t) val;
        break;
    }
    case S5L8702_TIMER_TDATA1_16(0):
    case S5L8702_TIMER_TDATA1_16(1):
    case S5L8702_TIMER_TDATA1_16(2):
    case S5L8702_TIMER_TDATA1_16(3):
    case S5L8702_TIMER_TDATA1_32(4):
    case S5L8702_TIMER_TDATA1_32(5):
    case S5L8702_TIMER_TDATA1_32(6):
    case S5L8702_TIMER_TDATA1_32(7): {
        trace_s5l8702_timer_write("tdata1", tidx, (uint32_t) val);
        t->tdata1 = (uint32_t) val;
        break;
    }
    case S5L8702_TIMER_TPRE_16(0):
    case S5L8702_TIMER_TPRE_16(1):
    case S5L8702_TIMER_TPRE_16(2):
    case S5L8702_TIMER_TPRE_16(3):
    case S5L8702_TIMER_TPRE_32(4):
    case S5L8702_TIMER_TPRE_32(5):
    case S5L8702_TIMER_TPRE_32(6):
    case S5L8702_TIMER_TPRE_32(7): {
        trace_s5l8702_timer_write("tpre", tidx, (uint32_t) val);
        s5l8702_timer_clk_select(t, t->tcon, (uint32_t) val);
        t->tpre = (uint32_t) val;
        break;
    }
    case S5L8702_TIMER_TCNT_16(0):
    case S5L8702_TIMER_TCNT_16(1):
    case S5L8702_TIMER_TCNT_16(2):
    case S5L8702_TIMER_TCNT_16(3):
    case S5L8702_TIMER_TCNT_32(4):
    case S5L8702_TIMER_TCNT_32(5):
    case S5L8702_TIMER_TCNT_32(6):
    case S5L8702_TIMER_TCNT_32(7): {
        trace_s5l8702_timer_write_tcnt_ro(tidx);
        break;
    }
    case S5L8702_TIMER_TSTAT: {
        trace_s5l8702_timer_write("tstat", 0, (uint32_t) val);
        /* Write-1-to-clear */
        s->tstat &= ~(uint32_t)val;
        
        /* FIX: Sync TCON flags for 32-bit timers to match TSTAT clears */
        for (uint32_t i = S5L8702_TIMER_COUNT_16; i < S5L8702_TIMER_COUNT; i++) {
            uint32_t shift = (i == 4) ? 8 : (i == 5) ? 16 : (i == 6) ? 24 : 0;
            if (!(s->tstat & (4 << shift))) s->timer[i].tcon &= ~S5L8702_TIMER_TCON_OVF;
            if (!(s->tstat & (1 << shift))) s->timer[i].tcon &= ~S5L8702_TIMER_TCON_INT0;
            if (!(s->tstat & (2 << shift))) s->timer[i].tcon &= ~S5L8702_TIMER_TCON_INT1;
            
            s5l8702_timer_update(&s->timer[i]);
        }
        return; 
    }
    default:
        trace_s5l8702_timer_write_unimp((uint32_t) offset);
    }

    s5l8702_timer_update(t);
}

static const MemoryRegionOps s5l8702_timer_ops = {
    .read = s5l8702_timer_read,
    .write = s5l8702_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_timer_realize(DeviceState *dev, Error **errp) {
    trace_s5l8702_timer_realize();
}

static void s5l8702_timer_reset(DeviceState *dev) {
    S5L8702TimerCtrlState *s = S5L8702_TIMER(dev);

    trace_s5l8702_timer_reset();

    s->tstat = 0;

    for (uint32_t i = 0; i < ARRAY_SIZE(s->timer); i++) {
        S5L8702Timer *t = &s->timer[i];
        timer_del(&t->timer);
        t->running = false;
        t->tcon = 0;
        t->tcmd = 0;
        t->tdata0 = 0;
        t->tdata1 = 0;
        t->tpre = 0;
        t->tcnt = 0;
        t->start_ns = 0;
        t->start_count = 0;
        t->sched_count = 0;
        t->sched_events = 0;
    }
}

static void s5l8702_timer_tick(void *opaque) {
    S5L8702Timer *t = opaque;

    trace_s5l8702_timer_tick();

    /* Advance the timing reference to the exact scheduled count */
    t->start_count = t->sched_count;
    t->start_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    t->tcnt = t->sched_count;

    /* Set interrupt flags for all events that fired this tick */
    if (t->sched_events & SCHED_EVT_INT0) {
        t->tcon |= S5L8702_TIMER_TCON_INT0;
    }
    if (t->sched_events & SCHED_EVT_INT1) {
        t->tcon |= S5L8702_TIMER_TCON_INT1;
    }
    if (t->sched_events & SCHED_EVT_OVF) {
        t->tcon |= S5L8702_TIMER_TCON_OVF;

        if ((t->tcon & S5L8702_TIMER_TCON_MODE_SEL_MASK) == S5L8702_TIMER_TCON_MODE_SEL(2)) {
            t->running = false;
        }
    }

    /* Update TSTAT for 32-bit timers (only set on fire, not on every update) */
    if (t->sched_events) s5l8702_timer_set_tstat(t);

    /* Raise / lower IRQ based on new flag state */
    s5l8702_timer_update(t);

    /* Reschedule for the next event if still running */
    if (t->running) s5l8702_timer_schedule(t);
}

/*
 * Find the nearest upcoming event (INT0 compare, INT1 compare, or overflow),
 * update the timing reference, and arm the QEMUTimer.
 */
static void s5l8702_timer_schedule(S5L8702Timer *t) {
    if (!t->running) return;

    uint64_t freq = s5l8702_timer_get_freq(t);
    if (freq == 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "s5l8702-timer: timer enabled with zero frequency\n");
        return;
    }

    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint32_t cur = s5l8702_timer_current_count(t);
    uint32_t max_val = s5l8702_timer_max_val(t);

    /* Re-anchor the start reference to avoid cumulative drift */
    t->start_ns = now;
    t->start_count = cur;

    /* Distance (in ticks) from cur to each event */
    uint64_t dist_int0 = s5l8702_dist_to_count(cur, t->tdata0, max_val);
    uint64_t dist_int1 = s5l8702_dist_to_count(cur, t->tdata1, max_val);
    uint64_t dist_ovf  = (uint64_t)(max_val - cur) + 1; /* always >= 1 */

    /* Schedule for the nearest event (ties fire simultaneously) */
    uint64_t min_dist = dist_ovf;
    if (dist_int0 < min_dist) { min_dist = dist_int0; }
    if (dist_int1 < min_dist) { min_dist = dist_int1; }

    /* Record which events fire at min_dist */
    t->sched_events = 0;
    if (dist_int0 == min_dist) { t->sched_events |= SCHED_EVT_INT0; }
    if (dist_int1 == min_dist) { t->sched_events |= SCHED_EVT_INT1; }
    if (dist_ovf  == min_dist) { t->sched_events |= SCHED_EVT_OVF;  }

    /* Record the counter value at the next fire point */
    t->sched_count = (uint32_t)((cur + min_dist) & (uint64_t)max_val);

    uint64_t ns = muldiv64(min_dist, NANOSECONDS_PER_SECOND, freq);
    timer_mod(&t->timer, now + ns);
}

static void s5l8702_timer_init(Object *obj) {
    S5L8702TimerCtrlState *s = S5L8702_TIMER(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    trace_s5l8702_timer_init();

    /* Two sysbus IRQ lines: index 0 = 16-bit group, index 1 = 32-bit group */
    sysbus_init_irq(sbd, &s->irq_16bit);
    sysbus_init_irq(sbd, &s->irq_32bit);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_timer_ops, s, TYPE_S5L8702_TIMER, S5L8702_TIMER_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    for (uint32_t i = 0; i < ARRAY_SIZE(s->timer); i++) {
        S5L8702Timer *t = &s->timer[i];
        t->ctrl = s;
        t->type = i < S5L8702_TIMER_COUNT_16 ? S5L8702_TIMER_TYPE_16 : S5L8702_TIMER_TYPE_32;
        timer_init_ns(&t->timer, QEMU_CLOCK_VIRTUAL, s5l8702_timer_tick, t);
    }
}

static void s5l8702_timer_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = s5l8702_timer_realize;
    dc->reset = s5l8702_timer_reset;
}

static const TypeInfo s5l8702_timer_types[] = {
    {
        .name = TYPE_S5L8702_TIMER,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_init = s5l8702_timer_init,
        .instance_size = sizeof(S5L8702TimerCtrlState),
        .class_init = s5l8702_timer_class_init,
    },
};
DEFINE_TYPES(s5l8702_timer_types);

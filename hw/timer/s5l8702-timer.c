#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/timer/s5l8702-timer.h"

#if 0
#define TIMER_LOG printf
#else
#define TIMER_LOG(...)
#endif

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

static void s5l8702_timer_update(S5L8702Timer *t) {
    // TODO
}

static void s5l8702_timer_clk_select(S5L8702Timer *t, uint32_t tcon, uint32_t tpre) {
    uint32_t prescale = (tpre & 0x3FF) + 1;

    if (tcon & S5L8702_TIMER_TCON_ECLK) {
        switch (tcon & S5L8702_TIMER_TCON_CS_MASK) {
        case S5L8702_TIMER_TCON_CS(0): // ECLK / 2
            TIMER_LOG("%s: tcon->cs = ECLK / 2\n", __func__);
            // clock_set_mul_div(&t->eclk, 1, 2 * prescale);
            break;
        case S5L8702_TIMER_TCON_CS(1): // ECLK / 4
            TIMER_LOG("%s: tcon->cs = ECLK / 4\n", __func__);
            // clock_set_mul_div(&t->eclk, 1, 4 * prescale);
            break;
        case S5L8702_TIMER_TCON_CS(2): // ECLK / 16
            TIMER_LOG("%s: tcon->cs = ECLK / 16\n", __func__);
            // clock_set_mul_div(&t->eclk, 1, 16 * prescale);
            break;
        case S5L8702_TIMER_TCON_CS(3): // ECLK / 64
            TIMER_LOG("%s: tcon->cs = ECLK / 64\n", __func__);
            // clock_set_mul_div(&t->eclk, 1, 64 * prescale);
            break;
        case S5L8702_TIMER_TCON_CS(4):
        case S5L8702_TIMER_TCON_CS(5): // External clock 0
            TIMER_LOG("%s: tcon->cs = external clock 0\n", __func__);
            // clock_set_mul_div(&t->extclk0, 1, 1 * prescale);
            break;
        case S5L8702_TIMER_TCON_CS(6):
        case S5L8702_TIMER_TCON_CS(7): // External clock 1
            TIMER_LOG("%s: tcon->cs = external clock 1\n", __func__);
            // clock_set_mul_div(&t->extclk1, 1, 1 * prescale);
            break;
        default: // Unsupported
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid tcon->cs value %d\n", __func__, ((uint32_t) tcon & S5L8702_TIMER_TCON_CS_MASK) >> 8);
        }
    } else {
        switch (tcon & S5L8702_TIMER_TCON_CS_MASK) {
        case S5L8702_TIMER_TCON_CS(0): // PCLK / 2
            TIMER_LOG("%s: tcon->cs = PCLK / 2\n", __func__);
            // clock_set_mul_div(&t->pclk, 1, 2 * prescale);
            break;
        case S5L8702_TIMER_TCON_CS(1): // PCLK / 4
            TIMER_LOG("%s: tcon->cs = PCLK / 4\n", __func__);
            // clock_set_mul_div(&t->pclk, 1, 4 * prescale);
            break;
        case S5L8702_TIMER_TCON_CS(2): // PCLK / 16
            TIMER_LOG("%s: tcon->cs = PCLK / 16\n", __func__);
            // clock_set_mul_div(&t->pclk, 1, 16 * prescale);
            break;
        case S5L8702_TIMER_TCON_CS(3): // PCLK / 64
            TIMER_LOG("%s: tcon->cs = PCLK / 64\n", __func__);
            // clock_set_mul_div(&t->pclk, 1, 64 * prescale);
            break;
        case S5L8702_TIMER_TCON_CS(4):
        case S5L8702_TIMER_TCON_CS(5): // External clock 0
            TIMER_LOG("%s: tcon->cs = external clock 0\n", __func__);
            // clock_set_mul_div(&t->extclk0, 1, 1 * prescale);
            break;
        case S5L8702_TIMER_TCON_CS(6):
        case S5L8702_TIMER_TCON_CS(7): // External clock 1
            TIMER_LOG("%s: tcon->cs = external clock 1\n", __func__);
            // clock_set_mul_div(&t->extclk1, 1, 1 * prescale);
            break;
        default: // Unsupported
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid tcon->cs value %d\n", __func__, ((uint32_t) tcon & S5L8702_TIMER_TCON_CS_MASK) >> 8);
        }
    }
}

static void s5l8702_timer_mode_select(S5L8702Timer *t, uint32_t tcon) {
    switch (tcon & S5L8702_TIMER_TCON_MODE_SEL_MASK) {
    case S5L8702_TIMER_TCON_MODE_SEL(0): // Interval mode
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented tcon->mode_sel = timer mode\n", __func__);
        break;
    case S5L8702_TIMER_TCON_MODE_SEL(1): // PWM mode
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented tcon->mode_sel = counter mode\n", __func__);
        break;
    case S5L8702_TIMER_TCON_MODE_SEL(2): // One-shot mode
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented tcon->mode_sel = pulse width measurement mode\n", __func__);
        break;
    case S5L8702_TIMER_TCON_MODE_SEL(3): // Capture mode
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented tcon->mode_sel = pulse period measurement mode\n", __func__);
        break;
    }
}

static void s5l8702_timer_clear(S5L8702Timer *t) {
    TIMER_LOG("%s\n", __func__);
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented\n", __func__);
}

static void s5l8702_timer_enable(S5L8702Timer *t, uint32_t tcmd) {
    TIMER_LOG("%s\n", __func__);
    if (tcmd & S5L8702_TIMER_TCMD_EN) {
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented tcmd->en\n", __func__);
    } else {
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented tcmd->dis\n", __func__);
    }
}

static uint32_t s5l8702_timer_get_cnt(S5L8702Timer *t) {
    // FIXME: This is just a hack to get the BL booting further
    t->tcnt += 128;
    return t->tcnt;
}

static uint64_t s5l8702_timer_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    S5L8702TimerCtrlState *s = S5L8702_TIMER(opaque);
    uint32_t tidx = offset / 0x20;
    if (tidx > S5L8702_TIMER_COUNT_16 + 1) tidx--; // 32-bit timers have a 0x20 offset
    S5L8702Timer *t = &s->timer[tidx];
    uint32_t r = 0;

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
        TIMER_LOG("s5l8702_timer_read: tcon[%d] = 0x%08x\n", tidx, r);
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
        TIMER_LOG("s5l8702_timer_read: tcmd[%d] = 0x%08x\n", tidx, r);
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
        TIMER_LOG("s5l8702_timer_read: tdata0[%d] = 0x%08x\n", tidx, r);
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
        TIMER_LOG("s5l8702_timer_read: tdata1[%d] = 0x%08x\n", tidx, r);
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
        TIMER_LOG("s5l8702_timer_read: tpre[%d] = 0x%08x\n", tidx, r);
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
        TIMER_LOG("s5l8702_timer_read: tcnt[%d] = 0x%08x\n", tidx, r);
        break;
    }
    case S5L8702_TIMER_TSTAT: {
        r = s->tstat;
        TIMER_LOG("s5l8702_timer_read: tstat = 0x%08x\n", r);
        break;
    }
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read offset 0x%04x\n",
                      __func__, (uint32_t) offset);
    }

    s5l8702_timer_update(t);

    return r;
}

static void s5l8702_timer_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    S5L8702TimerCtrlState *s = S5L8702_TIMER(opaque);
    uint32_t tidx = offset / 0x20;
    if (tidx > S5L8702_TIMER_COUNT_16 + 1) tidx--; // 32-bit timers have a 0x20 offset
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
        TIMER_LOG("s5l8702_timer_write: tcon[%d] = 0x%08x\n", tidx, (uint32_t) val);

        // TCON_OUT is read-only
        val &= ~S5L8702_TIMER_TCON_OUT;
        val |= t->tcon & S5L8702_TIMER_TCON_OUT;

        // TCON_OVF, TCON_INT1, TCON_INT0 are write 1 to clear
        if (val & S5L8702_TIMER_TCON_OVF) {
            val &= ~S5L8702_TIMER_TCON_OVF;
        } else {
            val |= t->tcon & S5L8702_TIMER_TCON_OVF;
        }

        if (val & S5L8702_TIMER_TCON_INT1) {
            val &= ~S5L8702_TIMER_TCON_INT1;
        } else {
            val |= t->tcon & S5L8702_TIMER_TCON_INT1;
        }

        if (val & S5L8702_TIMER_TCON_INT0) {
            val &= ~S5L8702_TIMER_TCON_INT0;
        } else {
            val |= t->tcon & S5L8702_TIMER_TCON_INT0;
        }

        if ((val & S5L8702_TIMER_TCON_OVF_EN) != (t->tcon & S5L8702_TIMER_TCON_OVF_EN)) {
            if (val & S5L8702_TIMER_TCON_OVF_EN) {
                qemu_log_mask(LOG_UNIMP, "%s: unimplemented overflow interrupt enable\n", __func__);
            } else {
                qemu_log_mask(LOG_UNIMP, "%s: unimplemented overflow interrupt disable\n", __func__);
            }
        }

        if ((val & S5L8702_TIMER_TCON_INT1_EN) != (t->tcon & S5L8702_TIMER_TCON_INT1_EN)) {
            if (val & S5L8702_TIMER_TCON_INT1_EN) {
                qemu_log_mask(LOG_UNIMP, "%s: unimplemented interrupt 1 enable\n", __func__);
            } else {
                qemu_log_mask(LOG_UNIMP, "%s: unimplemented interrupt 1 disable\n", __func__);
            }
        }

        if ((val & S5L8702_TIMER_TCON_INT0_EN) != (t->tcon & S5L8702_TIMER_TCON_INT0_EN)) {
            if (val & S5L8702_TIMER_TCON_INT0_EN) {
                qemu_log_mask(LOG_UNIMP, "%s: unimplemented interrupt 0 enable\n", __func__);
            } else {
                qemu_log_mask(LOG_UNIMP, "%s: unimplemented interrupt 0 disable\n", __func__);
            }
        }

        if ((val & S5L8702_TIMER_TCON_START) != (t->tcon & S5L8702_TIMER_TCON_START)) {
            qemu_log_mask(LOG_UNIMP, "%s: unimplemented tcon->start\n", __func__);
        }

        if ((val & S5L8702_TIMER_TCON_CS_MASK) != (t->tcon & S5L8702_TIMER_TCON_CS_MASK)) {
            s5l8702_timer_clk_select(t, val, t->tpre);
        }

        if ((val & S5L8702_TIMER_TCON_CAP_MODE) != (t->tcon & S5L8702_TIMER_TCON_CAP_MODE)) {
            qemu_log_mask(LOG_UNIMP, "%s: unimplemented tcon->cap_mode\n", __func__);
        }

        if ((val & S5L8702_TIMER_TCON_ECLK) != (t->tcon & S5L8702_TIMER_TCON_ECLK)) {
            s5l8702_timer_clk_select(t, val, t->tpre);
        }

        if ((val & S5L8702_TIMER_TCON_MODE_SEL_MASK) != (t->tcon & S5L8702_TIMER_TCON_MODE_SEL_MASK)) {
            s5l8702_timer_mode_select(t, val);
        }

        t->tcon = (uint32_t) val;
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

        if ((val & S5L8702_TIMER_TCMD_EN) != (t->tcmd & S5L8702_TIMER_TCMD_EN)) {
            s5l8702_timer_enable(t, val);
        }

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
        TIMER_LOG("s5l8702_timer_write: tdata0[%d] = 0x%08x\n", tidx, (uint32_t) val);
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
        TIMER_LOG("s5l8702_timer_write: tdata1[%d] = 0x%08x\n", tidx, (uint32_t) val);
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
        TIMER_LOG("s5l8702_timer_write: tpre[%d] = 0x%08x\n", tidx, (uint32_t) val);
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
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to read-only register tcnt[%d]\n", __func__, tidx);
        break;
    }
    case S5L8702_TIMER_TSTAT: {
        TIMER_LOG("s5l8702_timer_write: tstat = 0x%08x\n", (uint32_t) val);
        s->tstat = (uint32_t) val;
        break;
    }
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write offset 0x%04x\n",
                      __func__, (uint32_t) offset);
    }

    s5l8702_timer_update(t);
}

static const MemoryRegionOps s5l8702_timer_ops = {
    .read = s5l8702_timer_read,
    .write = s5l8702_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_timer_realize(DeviceState *dev, Error **errp)
{
    S5L8702TimerCtrlState *s = S5L8702_TIMER(dev);

    TIMER_LOG("s5l8702_timer_realize\n");

    for (uint32_t i = 0; i < ARRAY_SIZE(s->timer); i++) {
        S5L8702Timer *t = &s->timer[i];
        
        
    }
}

static void s5l8702_timer_reset(DeviceState *dev)
{
    S5L8702TimerCtrlState *s = S5L8702_TIMER(dev);

    TIMER_LOG("s5l8702_timer_reset\n");

    /* Reset registers */
    s->tstat = 0;

    for (uint32_t i = 0; i < ARRAY_SIZE(s->timer); i++) {
        S5L8702Timer *t = &s->timer[i];
        timer_del(&t->timer);
        t->tcon = 0;
        t->tcmd = 0;
        t->tdata0 = 0;
        t->tdata1 = 0;
        t->tpre = 0;
        t->tcnt = 0;
    }
}

static void s5l8702_timer_tick(void *opaque)
{
    S5L8702Timer *t = opaque;

    TIMER_LOG("s5l8702_timer_tick\n");
}

static void s5l8702_timer_init(Object *obj)
{
    S5L8702TimerCtrlState *s = S5L8702_TIMER(obj);

    TIMER_LOG("s5l8702_timer_init\n");

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_timer_ops, s, TYPE_S5L8702_TIMER, S5L8702_TIMER_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    for (uint32_t i = 0; i < ARRAY_SIZE(s->timer); i++) {
        S5L8702Timer *t = &s->timer[i];
        t->ctrl = s;
        t->type = i < 5 ? S5L8702_TIMER_TYPE_16 : S5L8702_TIMER_TYPE_32;
        timer_init_ns(&t->timer, QEMU_CLOCK_VIRTUAL, s5l8702_timer_tick, t);
        // TODO: irq
    }
}

static void s5l8702_timer_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
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

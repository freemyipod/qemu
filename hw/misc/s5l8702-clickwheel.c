#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/misc/s5l8702-clickwheel.h"
#include "trace.h"

/* Register offsets */
#define WHEEL00  0x00
#define WHEEL04  0x04
#define WHEEL08  0x08
#define WHEEL0C  0x0C
#define WHEEL10  0x10
#define WHEELINT 0x14
#define WHEELRX  0x18
#define WHEELTX  0x1C

#define WHEELINT_RX   (1 << 0)  /* RX data ready */
#define WHEELINT_TX   (1 << 1)  /* TX complete   */
#define WHEELINT_UNK  (1 << 2)  /* unknown       */

/*
 * Build a WHEELRX value from current GPIO button state.
 * Two formats are used on S5L8702:
 *   Init response:  (status & 0x8000FFFF) == 0x8000023A
 *                   buttons in bits [20:16]
 *   Normal update:  (status & 0x800000FF) == 0x8000001A
 *                   buttons in bits [12:8]
 */
static uint32_t build_rx_init(S5L8702ClickwheelState *s)
{
    uint32_t rx = 0x8000023A;
    if (s->gpio) {
        if (s->gpio->clickwheel_select_pressed) rx |= (1 << 16);
        if (s->gpio->clickwheel_next_pressed)   rx |= (1 << 17);
        if (s->gpio->clickwheel_prev_pressed)   rx |= (1 << 18);
        if (s->gpio->clickwheel_play_pressed)   rx |= (1 << 19);
        if (s->gpio->clickwheel_menu_pressed)   rx |= (1 << 20);
    }
    return rx;
}

static uint32_t build_rx_normal(S5L8702ClickwheelState *s)
{
    uint32_t rx = 0x8000001A;
    if (s->gpio) {
        if (s->gpio->clickwheel_select_pressed) rx |= (1 << 8);
        if (s->gpio->clickwheel_next_pressed)   rx |= (1 << 9);
        if (s->gpio->clickwheel_prev_pressed)   rx |= (1 << 10);
        if (s->gpio->clickwheel_play_pressed)   rx |= (1 << 11);
        if (s->gpio->clickwheel_menu_pressed)   rx |= (1 << 12);
    }
    return rx;
}

/* Called by one-shot timer after the firmware enables the controller */
static void clickwheel_init_cb(void *opaque){
    S5L8702ClickwheelState *s = S5L8702_CLICKWHEEL(opaque);

    trace_s5l8702_clickwheel_init_response();

    /* Deliver the init/"hello" response: data in WHEELRX, signal RX-ready */
    s->reg_rx = build_rx_init(s);
    s->reg_int |= WHEELINT_RX;
    qemu_irq_raise(s->irq);
}

/*
 * Public function: called by the key event handler (via a named GPIO input)
 * whenever button state changes.  Delivers a normal-type button update.
 */
static void s5l8702_clickwheel_button_update(void *opaque, int n, int level) {
    S5L8702ClickwheelState *s = S5L8702_CLICKWHEEL(opaque);

    if (!s->enabled) {
        return;
    }

    trace_s5l8702_clickwheel_button_update();

    /* Data in WHEELRX; signal RX-ready (bit 0) so the firmware reads it */
    s->reg_rx = build_rx_normal(s);
    s->reg_int |= WHEELINT_RX;
    qemu_irq_raise(s->irq);
}

static uint64_t s5l8702_clickwheel_read(void *opaque, hwaddr offset, unsigned size) {
    S5L8702ClickwheelState *s = S5L8702_CLICKWHEEL(opaque);

    switch (offset) {
    case WHEEL00:
        trace_s5l8702_clickwheel_reg_read("WHEEL00", s->reg_control);
        return s->reg_control;

    case WHEEL04:
        trace_s5l8702_clickwheel_reg_read("WHEEL04", s->reg_enable);
        return s->reg_enable;

    case WHEEL08:
        trace_s5l8702_clickwheel_reg_read("WHEEL08", s->reg_timing);
        return s->reg_timing;

    case WHEEL0C:
        trace_s5l8702_clickwheel_reg_read("WHEEL0C", s->reg_unk0c);
        return s->reg_unk0c;

    case WHEEL10:
        trace_s5l8702_clickwheel_reg_read("WHEEL10", s->reg_config);
        return s->reg_config;

    case WHEELINT:
        trace_s5l8702_clickwheel_reg_read("WHEELINT", s->reg_int);
        return s->reg_int;

    case WHEELRX:
        trace_s5l8702_clickwheel_reg_read("WHEELRX", s->reg_rx);
        return s->reg_rx;

    case WHEELTX:
        trace_s5l8702_clickwheel_reg_read("WHEELTX", s->reg_tx);
        return s->reg_tx;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read at offset 0x%04x\n", __func__, (uint32_t)offset);
        return 0;
    }
}

static void s5l8702_clickwheel_write(void *opaque, hwaddr offset, uint64_t val, unsigned size) {
    S5L8702ClickwheelState *s = S5L8702_CLICKWHEEL(opaque);

    switch (offset) {
    case WHEEL00:
        trace_s5l8702_clickwheel_reg_write("WHEEL00", (uint32_t)val);
        s->reg_control = val;
        if (val == 0) {
            /* Stop: disable and cancel any pending init timer */
            s->enabled = false;
            timer_del(s->init_timer);
        }
        break;

    case WHEEL04:
        trace_s5l8702_clickwheel_reg_write("WHEEL04", (uint32_t)val);
        s->reg_enable = val;
        if (val & 0x1) {
            /* Enable bit set: controller is now active */
            s->enabled = true;
            /*
             * If the firmware already wrote the init command (WHEELTX =
             * 0x8000023A) before enabling, fire the init response now.
             * Use a small delay to let the firmware finish its setup writes.
             */
            if (s->init_sent) {
                timer_mod_ns(s->init_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000);
            }
        } else {
            s->enabled = false;
        }
        break;

    case WHEEL08:
        trace_s5l8702_clickwheel_reg_write("WHEEL08", (uint32_t)val);
        s->reg_timing = val;
        break;

    case WHEEL0C:
        trace_s5l8702_clickwheel_reg_write("WHEEL0C", (uint32_t)val);
        s->reg_unk0c = val;
        break;

    case WHEEL10:
        trace_s5l8702_clickwheel_reg_write("WHEEL10", (uint32_t)val);
        s->reg_config = val;
        break;

    case WHEELINT:
        /*
         * Write-to-clear: firmware writes back the bits it wants to clear.
         * Lower interrupt line once all pending bits are cleared.
         */
        trace_s5l8702_clickwheel_reg_write("WHEELINT", (uint32_t)val);
        s->reg_int &= ~val;
        if (s->reg_int == 0) {
            qemu_irq_lower(s->irq);
        }
        break;

    case WHEELTX:
        trace_s5l8702_clickwheel_reg_write("WHEELTX", (uint32_t)val);
        s->reg_tx = val;
        /*
         * 0x8000023A is the init/poll command the firmware sends during
         * s5l_clickwheel_init().  Remember it was sent; if the controller is
         * already enabled schedule the response now, otherwise it will be
         * scheduled when WHEEL04 bit-0 is set.
         */
        if ((val & 0x8000FFFF) == 0x8000023A) {
            s->init_sent = true;
            if (s->enabled) {
                timer_mod_ns(s->init_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000);
            }
        }
        /*
         * Acknowledge the TX write immediately with a TX-complete interrupt
         * (bit 1).  The firmware clears this without reading WHEELRX.
         * The actual reply (init response or button data) arrives later
         * via the init_timer and raises bit 0 (RX ready).
         */
        s->reg_int |= WHEELINT_TX;
        qemu_irq_raise(s->irq);
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write at offset 0x%04x val=0x%08x\n", __func__, (uint32_t)offset, (uint32_t)val);
        break;
    }
}

static const MemoryRegionOps s5l8702_clickwheel_ops = {
    .read = s5l8702_clickwheel_read,
    .write = s5l8702_clickwheel_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void s5l8702_clickwheel_reset(DeviceState *dev) {
    S5L8702ClickwheelState *s = S5L8702_CLICKWHEEL(dev);

    trace_s5l8702_clickwheel_reset();

    timer_del(s->init_timer);

    s->reg_control = 0;
    s->reg_enable  = 0;
    s->reg_timing  = 0;
    s->reg_unk0c   = 0;
    s->reg_config  = 0;
    s->reg_int     = 0;
    s->reg_rx      = 0;
    s->reg_tx      = 0;
    s->enabled     = false;
    s->init_sent   = false;
}

static void s5l8702_clickwheel_realize(DeviceState *dev, Error **errp) {
    S5L8702ClickwheelState *s = S5L8702_CLICKWHEEL(dev);

    s->init_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, clickwheel_init_cb, s);
}

static void s5l8702_clickwheel_init(Object *obj) {
    S5L8702ClickwheelState *s = S5L8702_CLICKWHEEL(obj);

    trace_s5l8702_clickwheel_init();

    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_clickwheel_ops, s, TYPE_S5L8702_CLICKWHEEL, S5L8702_CLICKWHEEL_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    /* IRQ output to VIC */
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    /* Named GPIO input so key event handlers can signal button changes */
    qdev_init_gpio_in_named(DEVICE(obj), s5l8702_clickwheel_button_update, "button-update", 1);
}

static void s5l8702_clickwheel_finalize(Object *obj) {
    S5L8702ClickwheelState *s = S5L8702_CLICKWHEEL(obj);
    timer_free(s->init_timer);
}

static void s5l8702_clickwheel_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset   = s5l8702_clickwheel_reset;
    dc->realize = s5l8702_clickwheel_realize;
}

static const TypeInfo s5l8702_clickwheel_types[] = {
    {
        .name          = TYPE_S5L8702_CLICKWHEEL,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_init = s5l8702_clickwheel_init,
        .instance_finalize = s5l8702_clickwheel_finalize,
        .instance_size = sizeof(S5L8702ClickwheelState),
        .class_init    = s5l8702_clickwheel_class_init,
    },
};
DEFINE_TYPES(s5l8702_clickwheel_types);

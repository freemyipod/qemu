#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/misc/s5l8702-clickwheel.h"
#include "hw/qdev-properties.h"
#include "ui/input.h"
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
 *   Normal update:  (status & 0xBC0000FF) == 0x8000001A
 *                   buttons in bits [12:8], bit 30 = finger down,
 *                   bits [25:16] = wheel position (96 per revolution)
 */
static uint32_t build_rx_init(S5L8702ClickwheelState *s) {
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

static uint32_t build_rx_normal(S5L8702ClickwheelState *s) {
    uint32_t rx = 0x8000001A;
    if (s->gpio) {
        if (s->gpio->clickwheel_select_pressed) rx |= (1 << 8);
        if (s->gpio->clickwheel_next_pressed)   rx |= (1 << 9);
        if (s->gpio->clickwheel_prev_pressed)   rx |= (1 << 10);
        if (s->gpio->clickwheel_play_pressed)   rx |= (1 << 11);
        if (s->gpio->clickwheel_menu_pressed)   rx |= (1 << 12);
    }
    if (s->wheel_touched) {
        // Bit 30 is "finger is present" and the position is encoded in bits [25:16]
        rx |= (1u << 30) | ((s->wheel_pos & 0x3FF) << 16);
    }
    return rx;
}

static void clickwheel_update_irq(S5L8702ClickwheelState *s) {
    qemu_set_irq(s->irq, (s->reg_int != 0 && (s->reg_config & 1)) ? 1 : 0);
}

static bool clickwheel_enabled(S5L8702ClickwheelState *s) {
    return (s->reg_control & 0x00300000) != 0 || (s->reg_enable & 0x1) != 0;
}

static void clickwheel_deliver(S5L8702ClickwheelState *s) {
    s->reg_rx = build_rx_normal(s);
    s->reg_int |= WHEELINT_RX;
    clickwheel_update_irq(s);
}

static void clickwheel_init_cb(void *opaque){
    S5L8702ClickwheelState *s = S5L8702_CLICKWHEEL(opaque);

    trace_s5l8702_clickwheel_init_response();

    // Deliver the init/"hello" response: data in WHEELRX, signal RX-ready
    s->reg_rx = build_rx_init(s);
    s->reg_int |= WHEELINT_RX;
    clickwheel_update_irq(s);
}

/*
 * The scrolling stopped, so lift the finger: one more packet with bit 30
 * clear, which is what tells the firmware to re-arm its start-of-scroll
 * dead zone rather than treating the next touch as continued motion.
 */
static void clickwheel_release_cb(void *opaque) {
    S5L8702ClickwheelState *s = S5L8702_CLICKWHEEL(opaque);

    if (!s->wheel_touched) {
        return;
    }
    s->wheel_touched = false;
    trace_s5l8702_clickwheel_wheel_release(s->wheel_pos);

    if (clickwheel_enabled(s)) {
        clickwheel_deliver(s);
    }
}

/*
 * One click of the host scroll wheel = scroll_step positions around the
 * clickwheel, in the direction a finger would travel: scrolling down is
 * clockwise, which is increasing position.
 *
 * A real finger arrives before it moves, and the firmware needs that: the
 * first packet of a touch only establishes the reference position, and the
 * move after it has to clear a dead zone of 6 positions before RetailOS
 * counts it. So a touch always emits the reference packet first, and the
 * default step is comfortably past the dead zone.
 */
static void clickwheel_scroll(S5L8702ClickwheelState *s, int direction) {
    uint32_t step = s->scroll_step % S5L8702_CLICKWHEEL_POSITIONS;

    if (!clickwheel_enabled(s)) return;

    if (!s->wheel_touched) {
        s->wheel_touched = true;
        clickwheel_deliver(s);
    }

    s->wheel_pos = (s->wheel_pos + S5L8702_CLICKWHEEL_POSITIONS + direction * (int)step) % S5L8702_CLICKWHEEL_POSITIONS;
    trace_s5l8702_clickwheel_wheel_scroll(direction, s->wheel_pos);
    clickwheel_deliver(s);

    timer_mod_ns(s->release_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + (int64_t)s->release_ms * SCALE_MS);
}

static void clickwheel_input_event(DeviceState *dev, QemuConsole *src, InputEvent *evt) {
    S5L8702ClickwheelState *s = S5L8702_CLICKWHEEL(dev);
    InputBtnEvent *btn;

    if (evt->type != INPUT_EVENT_KIND_BTN) return;
    
    btn = evt->u.btn.data;
    if (!btn->down) return;

    switch (btn->button) {
    case INPUT_BUTTON_WHEEL_DOWN:
        clickwheel_scroll(s, +1);
        break;
    case INPUT_BUTTON_WHEEL_UP:
        clickwheel_scroll(s, -1);
        break;
    default:
        break;
    }
}

static const QemuInputHandler clickwheel_input_handler = {
    .name  = "iPod clickwheel",
    .mask  = INPUT_EVENT_MASK_BTN,
    .event = clickwheel_input_event,
};

static void s5l8702_clickwheel_button_update(void *opaque, int n, int level) {
    S5L8702ClickwheelState *s = S5L8702_CLICKWHEEL(opaque);

    if (!clickwheel_enabled(s)) return;

    trace_s5l8702_clickwheel_button_update();
    clickwheel_deliver(s);
}

static void s5l8702_clickwheel_enable_changed(S5L8702ClickwheelState *s, bool was_enabled) {
    bool now = clickwheel_enabled(s);

    if (now == was_enabled) {
        return;
    }
    s->enabled = now;

    if (now) {
        /* Small delay so the firmware can finish the rest of its setup
         * writes before the first packet lands. */
        timer_mod_ns(s->init_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000);
    } else {
        timer_del(s->init_timer);
        timer_del(s->release_timer);
        s->wheel_touched = false;
    }
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
        trace_s5l8702_clickwheel_reg_read("WHEEL0C", s->reg_int);
        return s->reg_int;

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
    case WHEEL00: {
        bool was_enabled = clickwheel_enabled(s);

        trace_s5l8702_clickwheel_reg_write("WHEEL00", (uint32_t)val);
        s->reg_control = val;
        s5l8702_clickwheel_enable_changed(s, was_enabled);
        break;
    }

    case WHEEL04: {
        bool was_enabled = clickwheel_enabled(s);

        trace_s5l8702_clickwheel_reg_write("WHEEL04", (uint32_t)val);
        s->reg_enable = val;
        s5l8702_clickwheel_enable_changed(s, was_enabled);
        break;
    }

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
        clickwheel_update_irq(s);
        break;

    case WHEELINT:
        trace_s5l8702_clickwheel_reg_write("WHEELINT", (uint32_t)val);
        s->reg_int &= ~val;
        clickwheel_update_irq(s);
        break;

    case WHEELTX:
        trace_s5l8702_clickwheel_reg_write("WHEELTX", (uint32_t)val);
        s->reg_tx = val;
        if ((val & 0x8000FFFF) == 0x8000023A) {
            s->init_sent = true;
            if (s->enabled) {
                timer_mod_ns(s->init_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000);
            }
        }
        s->reg_int |= WHEELINT_TX;
        clickwheel_update_irq(s);
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
    timer_del(s->release_timer);

    s->wheel_pos     = 0;
    s->wheel_touched = false;

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
    s->release_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, clickwheel_release_cb, s);

    /* The host scroll wheel drives the clickwheel. */
    s->input = qemu_input_handler_register(dev, &clickwheel_input_handler);
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
    timer_free(s->release_timer);
}

static Property s5l8702_clickwheel_properties[] = {
    /*
     * Wheel positions travelled per click of the host scroll wheel. RetailOS
     * moves one menu item per 6 positions, and ignores the first 6 positions
     * of a touch as a dead zone, so 6 is both the smallest step that moves
     * anything at all and the one that gives one item per click.
     */
    DEFINE_PROP_UINT32("scroll-step", S5L8702ClickwheelState, scroll_step, 6),
    /* How long after the last click the finger stays on the wheel. */
    DEFINE_PROP_UINT32("scroll-release-ms", S5L8702ClickwheelState, release_ms, 300),
    DEFINE_PROP_END_OF_LIST(),
};

static void s5l8702_clickwheel_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset   = s5l8702_clickwheel_reset;
    dc->realize = s5l8702_clickwheel_realize;
    device_class_set_props(dc, s5l8702_clickwheel_properties);
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

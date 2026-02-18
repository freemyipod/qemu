#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/gpio/s5l8702-gpio.h"
#include "trace.h"

#define S5L8702_GPIO_PCON(port)  (0x00000000 + (port << 5))
#define S5L8702_GPIO_PDAT(port)  (0x00000004 + (port << 5))
#define S5L8702_GPIO_PUNA(port)  (0x00000008 + (port << 5))
#define S5L8702_GPIO_PUNB(port)  (0x0000000C + (port << 5))
#define S5L8702_GPIO_PUNC(port)  (0x00000010 + (port << 5))
#define S5L8702_GPIO_GPIOCMD     0x00000200

static uint64_t s5l8702_gpio_read(void *opaque, hwaddr offset, unsigned size) {
    S5L8702GpioState *s = S5L8702_GPIO(opaque);
    const uint32_t port = offset >> 5;
    uint8_t r = 0;

    switch (offset) {
    case S5L8702_GPIO_PCON(0):
    case S5L8702_GPIO_PCON(1):
    case S5L8702_GPIO_PCON(2):
    case S5L8702_GPIO_PCON(3):
    case S5L8702_GPIO_PCON(4):
    case S5L8702_GPIO_PCON(5):
    case S5L8702_GPIO_PCON(6):
    case S5L8702_GPIO_PCON(7):
    case S5L8702_GPIO_PCON(8):
    case S5L8702_GPIO_PCON(9):
    case S5L8702_GPIO_PCON(10):
    case S5L8702_GPIO_PCON(11):
    case S5L8702_GPIO_PCON(12):
    case S5L8702_GPIO_PCON(13):
    case S5L8702_GPIO_PCON(14):
    case S5L8702_GPIO_PCON(15):
        r = s->pcon[port];
        trace_s5l8702_gpio_read("S5L8702_GPIO_PCON", port, r);
        break;
    case S5L8702_GPIO_PDAT(0):
    case S5L8702_GPIO_PDAT(1):
    case S5L8702_GPIO_PDAT(2):
    case S5L8702_GPIO_PDAT(3):
    case S5L8702_GPIO_PDAT(4):
    case S5L8702_GPIO_PDAT(5):
    case S5L8702_GPIO_PDAT(6):
    case S5L8702_GPIO_PDAT(7):
    case S5L8702_GPIO_PDAT(8):
    case S5L8702_GPIO_PDAT(9):
    case S5L8702_GPIO_PDAT(10):
    case S5L8702_GPIO_PDAT(11):
    case S5L8702_GPIO_PDAT(12):
    case S5L8702_GPIO_PDAT(13):
    case S5L8702_GPIO_PDAT(14):
        // this is the clickwheel code. brace yourself.
        // GPIOe.2 indicates who is talking: high = clickwheel, low = iPod
        // GPIOe.3 is the clock, likely driven by the clickwheel, data latch on rising edge
        // GPIOe.4 TX to clickwheel
        // GPIOe.5 RX from clickwheel

        r = 0x00;
        uint64_t ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

        if(s->clickwheel_clk) {
            s->clickwheel_clk = 0;
            if(s->gpio_pin_state[0xe][2] & 0x01) {
                s->clickwheel_bit_to_send = s->clickwheel_tx_buf & 1;
                s->clickwheel_tx_buf >>= 1;
                r |= s->clickwheel_bit_to_send << 5;
                s->clickwheel_skip_cycle = 1;
                trace_clickwheel_sending(s->clickwheel_bit_to_send, s->clickwheel_tx_buf, ns);
            }
        } else {
            if(s->clickwheel_skip_cycle) {
                s->clickwheel_skip_cycle = 0;
                trace_clickwheel_skip_cycle();
            } else {
                s->clickwheel_clk = 1;
                r |= 0b00001000;
            }

            if(s->gpio_pin_state[0xe][2] & 0x01) {
                r |= s->clickwheel_bit_to_send << 5;
                trace_clickwheel_sending(s->clickwheel_bit_to_send, s->clickwheel_tx_buf, ns);
            } else {
                // clickwheel is receiving, clock in data and check if we have a full command
                s->clickwheel_rx_buf <<= 1;
                s->clickwheel_rx_buf |= s->gpio_pin_state[0xe][4] & 0x01;
                trace_clickwheel_receiving(s->clickwheel_rx_buf, ns);
                switch(s->clickwheel_rx_buf) {
                    case 0xb8800003: // reverse of c000011d: read button presses
                        s->clickwheel_tx_buf = 0x8000023a;
                        
                        if(s->clickwheel_select_pressed) s->clickwheel_tx_buf |= (1 << 0x10);
                        if(s->clickwheel_play_pressed) s->clickwheel_tx_buf |= (1 << 0x11);
                        if(s->clickwheel_prev_pressed) s->clickwheel_tx_buf |= (1 << 0x12);
                        if(s->clickwheel_menu_pressed) s->clickwheel_tx_buf |= (1 << 0x13);
                        if(s->clickwheel_next_pressed) s->clickwheel_tx_buf |= (1 << 0x14);

                        s->clickwheel_skip_cycle = 1;
                        s->clickwheel_clk = 0;
                        trace_clickwheel_read_buttons(s->clickwheel_tx_buf);
                        break;
                    default:
                        break;
                }
            }
        }
        trace_clickwheel_out(r);
        break;
    case S5L8702_GPIO_PDAT(15):
        r = s->pdat[port];
        trace_s5l8702_gpio_read("S5L8702_GPIO_PDAT", port, r);
        if (port == 6) {
            r = 0x00;
        }
        break;
    case S5L8702_GPIO_PUNA(0):
    case S5L8702_GPIO_PUNA(1):
    case S5L8702_GPIO_PUNA(2):
    case S5L8702_GPIO_PUNA(3):
    case S5L8702_GPIO_PUNA(4):
    case S5L8702_GPIO_PUNA(5):
    case S5L8702_GPIO_PUNA(6):
    case S5L8702_GPIO_PUNA(7):
    case S5L8702_GPIO_PUNA(8):
    case S5L8702_GPIO_PUNA(9):
    case S5L8702_GPIO_PUNA(10):
    case S5L8702_GPIO_PUNA(11):
    case S5L8702_GPIO_PUNA(12):
    case S5L8702_GPIO_PUNA(13):
    case S5L8702_GPIO_PUNA(14):
    case S5L8702_GPIO_PUNA(15):
        r = s->puna[port];
        trace_s5l8702_gpio_read("S5L8702_GPIO_PUNA", port, r);
        break;
    case S5L8702_GPIO_PUNB(0):
    case S5L8702_GPIO_PUNB(1):
    case S5L8702_GPIO_PUNB(2):
    case S5L8702_GPIO_PUNB(3):
    case S5L8702_GPIO_PUNB(4):
    case S5L8702_GPIO_PUNB(5):
    case S5L8702_GPIO_PUNB(6):
    case S5L8702_GPIO_PUNB(7):
    case S5L8702_GPIO_PUNB(8):
    case S5L8702_GPIO_PUNB(9):
    case S5L8702_GPIO_PUNB(10):
    case S5L8702_GPIO_PUNB(11):
    case S5L8702_GPIO_PUNB(12):
    case S5L8702_GPIO_PUNB(13):
    case S5L8702_GPIO_PUNB(14):
    case S5L8702_GPIO_PUNB(15):
        r = s->punb[port];
        trace_s5l8702_gpio_read("S5L8702_GPIO_PUNB", port, r);
        break;
    case S5L8702_GPIO_PUNC(0):
    case S5L8702_GPIO_PUNC(1):
    case S5L8702_GPIO_PUNC(2):
    case S5L8702_GPIO_PUNC(3):
    case S5L8702_GPIO_PUNC(4):
    case S5L8702_GPIO_PUNC(5):
    case S5L8702_GPIO_PUNC(6):
    case S5L8702_GPIO_PUNC(7):
    case S5L8702_GPIO_PUNC(8):
    case S5L8702_GPIO_PUNC(9):
    case S5L8702_GPIO_PUNC(10):
    case S5L8702_GPIO_PUNC(11):
    case S5L8702_GPIO_PUNC(12):
    case S5L8702_GPIO_PUNC(13):
    case S5L8702_GPIO_PUNC(14):
    case S5L8702_GPIO_PUNC(15):
        r = s->punc[port];
        trace_s5l8702_gpio_read("S5L8702_GPIO_PUNA", port, r);
        break;
    case S5L8702_GPIO_GPIOCMD:
        r = s->gpiocmd;
        trace_s5l8702_gpio_read_cmd(r);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read (offset 0x%04x)\n", __func__, (uint32_t) offset);
    }

    return r;
}

static void s5l8702_gpio_write(void *opaque, hwaddr offset, uint64_t val, unsigned size) {
    S5L8702GpioState *s = S5L8702_GPIO(opaque);
    const uint32_t port = offset >> 5;

    switch (offset) {
    case S5L8702_GPIO_PCON(0):
    case S5L8702_GPIO_PCON(1):
    case S5L8702_GPIO_PCON(2):
    case S5L8702_GPIO_PCON(3):
    case S5L8702_GPIO_PCON(4):
    case S5L8702_GPIO_PCON(5):
    case S5L8702_GPIO_PCON(6):
    case S5L8702_GPIO_PCON(7):
    case S5L8702_GPIO_PCON(8):
    case S5L8702_GPIO_PCON(9):
    case S5L8702_GPIO_PCON(10):
    case S5L8702_GPIO_PCON(11):
    case S5L8702_GPIO_PCON(12):
    case S5L8702_GPIO_PCON(13):
    case S5L8702_GPIO_PCON(14):
    case S5L8702_GPIO_PCON(15):
        trace_s5l8702_gpio_write("S5L8702_GPIO_PCON", port, (uint32_t) val);
        s->pcon[port] = (uint8_t) val;
        break;
    case S5L8702_GPIO_PDAT(0):
    case S5L8702_GPIO_PDAT(1):
    case S5L8702_GPIO_PDAT(2):
    case S5L8702_GPIO_PDAT(3):
    case S5L8702_GPIO_PDAT(4):
    case S5L8702_GPIO_PDAT(5):
    case S5L8702_GPIO_PDAT(6):
    case S5L8702_GPIO_PDAT(7):
    case S5L8702_GPIO_PDAT(8):
    case S5L8702_GPIO_PDAT(9):
    case S5L8702_GPIO_PDAT(10):
    case S5L8702_GPIO_PDAT(11):
    case S5L8702_GPIO_PDAT(12):
    case S5L8702_GPIO_PDAT(13):
    case S5L8702_GPIO_PDAT(14):
    case S5L8702_GPIO_PDAT(15):
        trace_s5l8702_gpio_write("S5L8702_GPIO_PDAT", port, (uint32_t) val);
        s->pdat[port] = (uint8_t) val;
        for (int i = 0; i < 8; i++) {
            qemu_set_irq(s->output[port * 8 + i], (s->pdat[port] >> i) & 1);
        }
        break;
    case S5L8702_GPIO_PUNA(0):
    case S5L8702_GPIO_PUNA(1):
    case S5L8702_GPIO_PUNA(2):
    case S5L8702_GPIO_PUNA(3):
    case S5L8702_GPIO_PUNA(4):
    case S5L8702_GPIO_PUNA(5):
    case S5L8702_GPIO_PUNA(6):
    case S5L8702_GPIO_PUNA(7):
    case S5L8702_GPIO_PUNA(8):
    case S5L8702_GPIO_PUNA(9):
    case S5L8702_GPIO_PUNA(10):
    case S5L8702_GPIO_PUNA(11):
    case S5L8702_GPIO_PUNA(12):
    case S5L8702_GPIO_PUNA(13):
    case S5L8702_GPIO_PUNA(14):
    case S5L8702_GPIO_PUNA(15):
        trace_s5l8702_gpio_write("S5L8702_GPIO_PUNA", port, (uint32_t) val);
        s->puna[port] = (uint8_t) val;
        break;
    case S5L8702_GPIO_PUNB(0):
    case S5L8702_GPIO_PUNB(1):
    case S5L8702_GPIO_PUNB(2):
    case S5L8702_GPIO_PUNB(3):
    case S5L8702_GPIO_PUNB(4):
    case S5L8702_GPIO_PUNB(5):
    case S5L8702_GPIO_PUNB(6):
    case S5L8702_GPIO_PUNB(7):
    case S5L8702_GPIO_PUNB(8):
    case S5L8702_GPIO_PUNB(9):
    case S5L8702_GPIO_PUNB(10):
    case S5L8702_GPIO_PUNB(11):
    case S5L8702_GPIO_PUNB(12):
    case S5L8702_GPIO_PUNB(13):
    case S5L8702_GPIO_PUNB(14):
    case S5L8702_GPIO_PUNB(15):
        trace_s5l8702_gpio_write("S5L8702_GPIO_PUNB", port, (uint32_t) val);
        s->punb[port] = (uint8_t) val;
        break;
    case S5L8702_GPIO_PUNC(0):
    case S5L8702_GPIO_PUNC(1):
    case S5L8702_GPIO_PUNC(2):
    case S5L8702_GPIO_PUNC(3):
    case S5L8702_GPIO_PUNC(4):
    case S5L8702_GPIO_PUNC(5):
    case S5L8702_GPIO_PUNC(6):
    case S5L8702_GPIO_PUNC(7):
    case S5L8702_GPIO_PUNC(8):
    case S5L8702_GPIO_PUNC(9):
    case S5L8702_GPIO_PUNC(10):
    case S5L8702_GPIO_PUNC(11):
    case S5L8702_GPIO_PUNC(12):
    case S5L8702_GPIO_PUNC(13):
    case S5L8702_GPIO_PUNC(14):
    case S5L8702_GPIO_PUNC(15):
        trace_s5l8702_gpio_write("S5L8702_GPIO_PUNC", port, (uint32_t) val);
        s->punc[port] = (uint8_t) val;
        break;
    case S5L8702_GPIO_GPIOCMD:
        uint8_t set = (val & 0x00FF0000) >> 16;
        uint8_t pin = (val & 0x0000FF00) >> 8;
        uint8_t state = (val & 0x000000FF);
        
        s->gpio_pin_state[set][pin] = state;
        s->gpiocmd = state;
        trace_s5l8702_gpio_write_cmd(set, pin, state);
        if ((s->gpiocmd & ~1) == 0x0000e) {
            qemu_set_irq(s->output[0], s->gpiocmd & 1);
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write (offset 0x%04x, value 0x%08x)\n",
                      __func__, (uint32_t) offset, (uint32_t) val);
    }
}

static const MemoryRegionOps s5l8702_gpio_ops = {
    .read = s5l8702_gpio_read,
    .write = s5l8702_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_gpio_set(void *opaque, int n, int level) {
    S5L8702GpioState *s = S5L8702_GPIO(opaque);
    const uint32_t port = S5L8702_GPIO_PORT(n);

    if (level) {
        s->pdat[port] |= (1 << S5L8702_GPIO_PIN(n));
    } else {
        s->pdat[port] &= ~(1 << S5L8702_GPIO_PIN(n));
    }
}

static void s5l8702_gpio_reset(DeviceState *dev) {
    S5L8702GpioState *s = S5L8702_GPIO(dev);

    /* Set default values for registers */
    memset(s->pcon, 0, sizeof(s->pcon));
    memset(s->pdat, 0, sizeof(s->pdat));
    memset(s->puna, 0, sizeof(s->puna));
    memset(s->punb, 0, sizeof(s->punb));
    memset(s->punc, 0, sizeof(s->punc));
}

static void s5l8702_gpio_init(Object *obj) {
    S5L8702GpioState *s = S5L8702_GPIO(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_gpio_ops, s, TYPE_S5L8702_GPIO, S5L8702_GPIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    qdev_init_gpio_in(DEVICE(s), s5l8702_gpio_set, S5L8702_GPIO_PINS);
    qdev_init_gpio_out(DEVICE(s), s->output, S5L8702_GPIO_PINS);
}

static void s5l8702_gpio_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = s5l8702_gpio_reset;
}

static const TypeInfo s5l8702_gpio_types[] = {
    {
        .name = TYPE_S5L8702_GPIO,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(S5L8702GpioState),
        .instance_init = s5l8702_gpio_init,
        .class_init = s5l8702_gpio_class_init,
    },
};
DEFINE_TYPES(s5l8702_gpio_types);

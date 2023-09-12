#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/gpio/s5l8702-gpio.h"

#define S5L8702_GPIO_PCON(port)  (0x00000000 + (port << 5))
#define S5L8702_GPIO_PDAT(port)  (0x00000004 + (port << 5))
#define S5L8702_GPIO_PUNA(port)  (0x00000008 + (port << 5))
#define S5L8702_GPIO_PUNB(port)  (0x0000000C + (port << 5))
#define S5L8702_GPIO_PUNC(port)  (0x00000010 + (port << 5))
#define S5L8702_GPIO_GPIOCMD     0x00000200

static uint64_t s5l8702_gpio_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    const S5L8702GpioState *s = S5L8702_GPIO(opaque);
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
        printf("s5l8702_gpio_read: S5L8702_GPIO_PCON%d = 0x%08x\n", port, r);
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
        r = s->pdat[port];
        printf("s5l8702_gpio_read: S5L8702_GPIO_PDAT%d = 0x%08x\n", port, r);
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
        printf("s5l8702_gpio_read: S5L8702_GPIO_PUNA%d = 0x%08x\n", port, r);
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
        printf("s5l8702_gpio_read: S5L8702_GPIO_PUNB%d = 0x%08x\n", port, r);
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
        printf("s5l8702_gpio_read: S5L8702_GPIO_PUNC%d = 0x%08x\n", port, r);
        break;
    case S5L8702_GPIO_GPIOCMD:
        r = s->gpiocmd;
        printf("s5l8702_gpio_read: S5L8702_GPIO_GPIOCMD = 0x%08x\n", r);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read (offset 0x%04x)\n",
                      __func__, (uint32_t) offset);
    }

    return r;
}

static void s5l8702_gpio_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
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
        printf("s5l8702_gpio_write: S5L8702_GPIO_PCON%d = 0x%08x\n", port, (uint32_t) val);
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
        printf("s5l8702_gpio_write: S5L8702_GPIO_PDAT%d = 0x%08x\n", port, (uint32_t) val);
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
        printf("s5l8702_gpio_write: S5L8702_GPIO_PUNA%d = 0x%08x\n", port, (uint32_t) val);
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
        printf("s5l8702_gpio_write: S5L8702_GPIO_PUNB%d = 0x%08x\n", port, (uint32_t) val);
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
        printf("s5l8702_gpio_write: S5L8702_GPIO_PUNC%d = 0x%08x\n", port, (uint32_t) val);
        s->punc[port] = (uint8_t) val;
        break;
    case S5L8702_GPIO_GPIOCMD:
        printf("s5l8702_gpio_write: S5L8702_GPIO_GPIOCMD: old: 0x%08x, new: 0x%08x\n", s->gpiocmd, (uint8_t) val);
        s->gpiocmd = (uint8_t) val;
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

static void s5l8702_gpio_set(void *opaque, int n, int level)
{
    S5L8702GpioState *s = S5L8702_GPIO(opaque);
    const uint32_t port = S5L8702_GPIO_PORT(n);

    if (level) {
        s->pdat[port] |= (1 << S5L8702_GPIO_PIN(n));
    } else {
        s->pdat[port] &= ~(1 << S5L8702_GPIO_PIN(n));
    }
}

static void s5l8702_gpio_reset(DeviceState *dev)
{
    S5L8702GpioState *s = S5L8702_GPIO(dev);

    /* Set default values for registers */
    memset(s->pcon, 0, sizeof(s->pcon));
    memset(s->pdat, 0, sizeof(s->pdat));
    memset(s->puna, 0, sizeof(s->puna));
    memset(s->punb, 0, sizeof(s->punb));
    memset(s->punc, 0, sizeof(s->punc));
}

static void s5l8702_gpio_init(Object *obj)
{
    S5L8702GpioState *s = S5L8702_GPIO(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_gpio_ops, s, TYPE_S5L8702_GPIO, S5L8702_GPIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    qdev_init_gpio_in(DEVICE(s), s5l8702_gpio_set, S5L8702_GPIO_PINS);
    qdev_init_gpio_out(DEVICE(s), s->output, S5L8702_GPIO_PINS);
}

static void s5l8702_gpio_class_init(ObjectClass *klass, void *data)
{
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

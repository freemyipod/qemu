#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/s5l8702-sysic.h"
#include "hw/irq.h"
#include "trace.h"

/* Power Management */
#define SYSIC_POWER_CONFIG   0x00
#define SYSIC_POWER_SETSTATE 0x08
#define SYSIC_POWER_ONCTRL   0x0C
#define SYSIC_POWER_OFFCTRL  0x10
#define SYSIC_POWER_STATE    0x14
#define SYSIC_POWER_ID       0x44

#define POWER_ID_ADM         0x10

/* GPIO Interrupt Controller */
#define GPIO_INTLEVEL 0x80
#define GPIO_INTSTAT  0xA0
#define GPIO_INTEN    0xC0
#define GPIO_INTTYPE  0xE0

// this peripheral is just a guess based on davos50's iPod Touch 1G code.
// it makes sense here too, so I rewrote it in the style of this fork but
// the logic is the same.

static uint64_t s5l8702_sysic_read(void *opaque, hwaddr addr, unsigned size) {
    S5L8702SysICState *s = S5L8702_SYSIC(opaque);
    uint32_t val = 0;

    switch (addr) {
    /*
     * osos's sleep routine (in IRAM at 0x22002d78) read-modify-writes this
     * register and then spins, with interrupts disabled, until bit 0 reads
     * back set -- so returning a constant 0 wedges the CPU for good, leaving
     * the CLCD showing the last frame it composed and the machine looking
     * merely unresponsive. A plain latch is enough: the firmware sets the bit
     * itself and only wants to see it take.
     */
    case SYSIC_POWER_CONFIG:
        val = s->power_config;
        break;
    case SYSIC_POWER_ID:
        val = (2 << 0x18);
        break;
    case SYSIC_POWER_SETSTATE:
    case SYSIC_POWER_STATE:
        val = s->power_state;
        break;
    case 0x7a:
    case 0x7c:
        val = 1;
        break;
    case GPIO_INTLEVEL ... (GPIO_INTLEVEL + (S5L8702_SYSIC_GPIO_GROUPS - 1) * 4):
        val = s->gpio_int_level[(addr - GPIO_INTLEVEL) / 4];
        break;
    case GPIO_INTSTAT ... (GPIO_INTSTAT + (S5L8702_SYSIC_GPIO_GROUPS - 1) * 4):
        val = s->gpio_int_status[(addr - GPIO_INTSTAT) / 4];
        break;
    case GPIO_INTEN ... (GPIO_INTEN + (S5L8702_SYSIC_GPIO_GROUPS - 1) * 4):
        val = s->gpio_int_enabled[(addr - GPIO_INTEN) / 4];
        break;
    case GPIO_INTTYPE ... (GPIO_INTTYPE + (S5L8702_SYSIC_GPIO_GROUPS - 1) * 4):
        val = s->gpio_int_type[(addr - GPIO_INTTYPE) / 4];
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read at 0x%04x\n", __func__, (uint32_t)addr);
        break;
    }

    trace_s5l8702_sysic_read((uint32_t)addr, val);
    return val;
}

static void s5l8702_sysic_write(void *opaque, hwaddr addr, uint64_t val, unsigned size) {
    S5L8702SysICState *s = S5L8702_SYSIC(opaque);
    uint8_t group;

    trace_s5l8702_sysic_write((uint32_t)addr, (uint32_t)val);

    switch (addr) {
    case SYSIC_POWER_CONFIG:
        s->power_config = val;
        break;
    case SYSIC_POWER_ONCTRL:
        trace_s5l8702_sysic_power_onctrl((uint32_t)val);
        if ((val & 0x20) != 0 || (val & 0x4) != 0 || (val & POWER_ID_ADM) != 0) {
            break;
        }
        s->power_state = val;
        break;
    case SYSIC_POWER_OFFCTRL:
        trace_s5l8702_sysic_power_offctrl((uint32_t)val);
        s->power_state = val;
        break;
    case GPIO_INTLEVEL ... (GPIO_INTLEVEL + (S5L8702_SYSIC_GPIO_GROUPS - 1) * 4):
        /* Read-only or handled elsewhere usually */
        break;
    case GPIO_INTSTAT ... (GPIO_INTSTAT + (S5L8702_SYSIC_GPIO_GROUPS - 1) * 4):
        group = (addr - GPIO_INTSTAT) / 4;
        /* Write-1-to-clear interrupt status */
        trace_s5l8702_sysic_gpio_intstat_clear(group, (uint32_t)val);
        s->gpio_int_status[group] &= ~val;
        if (s->gpio_int_status[group] == 0) {
            qemu_irq_lower(s->gpio_irqs[group]);
            trace_s5l8702_sysic_gpio_irq_lower(group);
        }
        break;
    case GPIO_INTEN ... (GPIO_INTEN + (S5L8702_SYSIC_GPIO_GROUPS - 1) * 4):
        group = (addr - GPIO_INTEN) / 4;
        trace_s5l8702_sysic_gpio_inten_set(group, (uint32_t)val);
        s->gpio_int_enabled[group] = val;
        break;
    case GPIO_INTTYPE ... (GPIO_INTTYPE + (S5L8702_SYSIC_GPIO_GROUPS - 1) * 4):
        group = (addr - GPIO_INTTYPE) / 4;
        trace_s5l8702_sysic_gpio_inttype_set(group, (uint32_t)val);
        s->gpio_int_type[group] = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write at 0x%04x = 0x%08x\n", 
                      __func__, (uint32_t)addr, (uint32_t)val);
        break;
    }
}

static const MemoryRegionOps s5l8702_sysic_ops = {
    .read = s5l8702_sysic_read,
    .write = s5l8702_sysic_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void s5l8702_sysic_reset(DeviceState *dev) {
    S5L8702SysICState *s = S5L8702_SYSIC(dev);

    trace_s5l8702_sysic_reset();
    s->power_state = 0;
    s->usb_connected = false;
    for (int i = 0; i < S5L8702_SYSIC_GPIO_GROUPS; i++) {
        s->gpio_int_level[i] = 0;
        s->gpio_int_status[i] = 0;
        s->gpio_int_enabled[i] = 0;
        s->gpio_int_type[i] = 0;
        qemu_irq_lower(s->gpio_irqs[i]);
    }
}

static void s5l8702_sysic_init(Object *obj) {
    S5L8702SysICState *s = S5L8702_SYSIC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    trace_s5l8702_sysic_init();
    memory_region_init_io(&s->iomem, obj, &s5l8702_sysic_ops, s, TYPE_S5L8702_SYSIC, 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    
    for (int i = 0; i < S5L8702_SYSIC_GPIO_GROUPS; i++) {
        sysbus_init_irq(sbd, &s->gpio_irqs[i]);
    }
}

static void s5l8702_sysic_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = s5l8702_sysic_reset;
}

static const TypeInfo s5l8702_sysic_type_info = {
    .name = TYPE_S5L8702_SYSIC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S5L8702SysICState),
    .instance_init = s5l8702_sysic_init,
    .class_init = s5l8702_sysic_class_init,
};

static void s5l8702_sysic_register_types(void) {
    type_register_static(&s5l8702_sysic_type_info);
}

type_init(s5l8702_sysic_register_types)
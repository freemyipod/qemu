#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/s5l8702-usbphy.h"
#include "trace.h"

#define REG_INDEX(offset) ((offset) / sizeof(uint32_t))

static uint64_t s5l8702_usbphy_read(void *opaque, hwaddr offset, unsigned size) {
    const S5L8702UsbPhyState *s = S5L8702_USBPHY(opaque);
    const uint32_t idx = REG_INDEX(offset);
    uint32_t val = s->regs[idx];

    trace_s5l8702_usbphy_read((uint32_t)offset, val);
    return val;
}

static void s5l8702_usbphy_write(void *opaque, hwaddr offset, uint64_t value, unsigned size) {
    S5L8702UsbPhyState *s = S5L8702_USBPHY(opaque);
    const uint32_t idx = REG_INDEX(offset);

    trace_s5l8702_usbphy_write((uint32_t)offset, (uint32_t)value);
    s->regs[idx] = (uint32_t)value;
}

static const MemoryRegionOps s5l8702_usbphy_ops = {
    .read = s5l8702_usbphy_read,
    .write = s5l8702_usbphy_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_usbphy_reset(DeviceState *dev) {
    S5L8702UsbPhyState *s = S5L8702_USBPHY(dev);

    trace_s5l8702_usbphy_reset();
    memset(s->regs, 0, sizeof(s->regs));

    // Uncomment this line to force DFU mode on boot by default
    // s->regs[10] = 0x00000001;
}

static void s5l8702_usbphy_init(Object *obj) {
    S5L8702UsbPhyState *s = S5L8702_USBPHY(obj);

    trace_s5l8702_usbphy_init();

    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_usbphy_ops, s, TYPE_S5L8702_USBPHY, S5L8702_USBPHY_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    // Uncomment this line to force DFU mode on boot by default
    // s->regs[10] = 0x00000001;
}

static void s5l8702_usbphy_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = s5l8702_usbphy_reset;
}

static const TypeInfo s5l8702_usbphy_types[] = {
    {
        .name          = TYPE_S5L8702_USBPHY,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(S5L8702UsbPhyState),
        .instance_init = s5l8702_usbphy_init,
        .class_init    = s5l8702_usbphy_class_init,
    },
};
DEFINE_TYPES(s5l8702_usbphy_types);

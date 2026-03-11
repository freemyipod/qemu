#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/s5l8702-usbotg.h"
#include "trace.h"

#define REG_INDEX(offset) ((offset) / sizeof(uint32_t))

static uint64_t s5l8702_usbotg_read(void *opaque, hwaddr offset, unsigned size) {
    const S5L8702UsbOtgState *s = S5L8702_USBOTG(opaque);
    const uint32_t idx = REG_INDEX(offset);
    uint32_t val = s->regs[idx];

    switch (offset) {
        case 0x440:
            /* 
             * Return J-State in PrtLnSts (Bits 11:10 = 01)
             * (1 << 10) = 0x400. 
             * Let's also set Bit 0 (Current Connect Status) to 1 just in case.
             */
            return 0x00000401; 
        default:
            return 0;
    }

    trace_s5l8702_usbotg_read((uint32_t)offset, val);
    return val;
}

static void s5l8702_usbotg_write(void *opaque, hwaddr offset, uint64_t value, unsigned size) {
    S5L8702UsbOtgState *s = S5L8702_USBOTG(opaque);
    const uint32_t idx = REG_INDEX(offset);

    trace_s5l8702_usbotg_write((uint32_t)offset, (uint32_t)value);
    s->regs[idx] = (uint32_t)value;
}

static const MemoryRegionOps s5l8702_usbotg_ops = {
    .read = s5l8702_usbotg_read,
    .write = s5l8702_usbotg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_usbotg_reset(DeviceState *dev) {
    S5L8702UsbOtgState *s = S5L8702_USBOTG(dev);

    trace_s5l8702_usbotg_reset();
    memset(s->regs, 0, sizeof(s->regs));
}

static void s5l8702_usbotg_init(Object *obj) {
    S5L8702UsbOtgState *s = S5L8702_USBOTG(obj);

    trace_s5l8702_usbotg_init();

    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_usbotg_ops, s,
                          TYPE_S5L8702_USBOTG, S5L8702_USBOTG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void s5l8702_usbotg_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = s5l8702_usbotg_reset;
}

static const TypeInfo s5l8702_usbotg_types[] = {
    {
        .name          = TYPE_S5L8702_USBOTG,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(S5L8702UsbOtgState),
        .instance_init = s5l8702_usbotg_init,
        .class_init    = s5l8702_usbotg_class_init,
    },
};
DEFINE_TYPES(s5l8702_usbotg_types);

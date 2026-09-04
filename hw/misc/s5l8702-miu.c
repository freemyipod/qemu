#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/s5l8702-miu.h"
#include "trace.h"

#define REG_INDEX(offset) ((offset) / sizeof(uint32_t))

/*
 * MIUCON's bit 2 is the address-0 remap: with it set the CPU sees IRAM0 at 0
 * instead of the BootROM, which is how firmware gets its own exception vectors
 * installed (it builds them at 0x22000000 first). rockbox's miu_preinit()
 * writes 0x1000100D on a nano 3G and documents it as "remap = 1 (IRAM mapped
 * to 0x0)"; that value sets bits 0 and 2, so it does not say which one carries
 * the remap on its own.
 */
#define S5L8702_MIUCON          0x00
#define S5L8702_MIUCON_REMAP    BIT(2)

static uint64_t s5l8702_miu_read(void *opaque, hwaddr offset, unsigned size) {
    const S5L8702MiuState *s = S5L8702_MIU(opaque);
    const uint32_t idx = REG_INDEX(offset);
    uint32_t val = s->regs[idx];

    trace_s5l8702_miu_read((uint32_t)offset, val);
    return val;
}

static void s5l8702_miu_write(void *opaque, hwaddr offset, uint64_t value, unsigned size) {
    S5L8702MiuState *s = S5L8702_MIU(opaque);
    const uint32_t idx = REG_INDEX(offset);

    trace_s5l8702_miu_write((uint32_t)offset, (uint32_t)value);
    s->regs[idx] = (uint32_t)value;

    if (offset == S5L8702_MIUCON) {
        s5l8702_buscon_select(s->buscon, !!(value & S5L8702_MIUCON_REMAP));
    }
}

static const MemoryRegionOps s5l8702_miu_ops = {
    .read = s5l8702_miu_read,
    .write = s5l8702_miu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_miu_reset(DeviceState *dev) {
    S5L8702MiuState *s = S5L8702_MIU(dev);

    trace_s5l8702_miu_reset();
    memset(s->regs, 0, sizeof(s->regs));
}

static void s5l8702_miu_init(Object *obj) {
    S5L8702MiuState *s = S5L8702_MIU(obj);

    trace_s5l8702_miu_init();

    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_miu_ops, s, TYPE_S5L8702_MIU, S5L8702_MIU_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void s5l8702_miu_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = s5l8702_miu_reset;
}

static const TypeInfo s5l8702_miu_types[] = {
    {
        .name          = TYPE_S5L8702_MIU,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(S5L8702MiuState),
        .instance_init = s5l8702_miu_init,
        .class_init    = s5l8702_miu_class_init,
    },
};
DEFINE_TYPES(s5l8702_miu_types);

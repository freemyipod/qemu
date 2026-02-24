#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/s5l8702-chipid.h"
#include "trace.h"

static uint64_t s5l8702_chipid_read(void *opaque, hwaddr offset, unsigned size) {
    switch (offset) {
        case 0x0:
            return (S5L8702_CHIP_REVISION << 24);
        default:
            qemu_log_mask(LOG_UNIMP, "%s: unimplemented read at offset 0x%" HWADDR_PRIx "\n", __func__, offset);
            break;
    }

    return 0;
}

static void s5l8702_chipid_write(void *opaque, hwaddr offset, uint64_t value, unsigned size) {
    qemu_log_mask(LOG_UNIMP, "%s: unimplemented write at offset 0x%" HWADDR_PRIx " value 0x%" PRIx64 "\n", __func__, offset, value);
}

static const MemoryRegionOps s5l8702_chipid_ops = {
    .read = s5l8702_chipid_read,
    .write = s5l8702_chipid_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_chipid_init(Object *obj) {
    S5L8702ChipIDState *s = S5L8702_CHIPID(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &s5l8702_chipid_ops, s, TYPE_S5L8702_CHIPID, S5L8702_CHIPID_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void s5l8702_chipid_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    (void)dc;
}

static const TypeInfo s5l8702_chipid_types[] = {
    {
        .name          = TYPE_S5L8702_CHIPID,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(S5L8702ChipIDState),
        .instance_init = s5l8702_chipid_init,
        .class_init    = s5l8702_chipid_class_init,
    },
};
DEFINE_TYPES(s5l8702_chipid_types);

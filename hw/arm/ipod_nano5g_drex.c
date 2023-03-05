#include "hw/arm/ipod_nano5g_drex.h"

static void S5L8730_drex_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IPodNano5GDREXState *s = (struct IPodNano3GClockState *) opaque;
    //printf("DREX write %lx <- %lx\n", addr, val);
}

static uint64_t S5L8730_drex_read(void *opaque, hwaddr addr, unsigned size)
{
    IPodNano5GDREXState *s = (struct IPodNano5GDREXState *) opaque;
    //printf("DREX read %lx\n", addr);
    switch (addr) {
        case 0x140:
            return 1<<1;
        case 0x144:
            return (1<<1)|(1<<0);
    }
    return 0;
}


static const MemoryRegionOps drex_ops = {
    .read = S5L8730_drex_read,
    .write = S5L8730_drex_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void S5L8730_drex_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(sbd);
    IPodNano5GDREXState *s = IPOD_NANO5G_DREX(dev);

    memory_region_init_io(&s->iomem, obj, &drex_ops, s, "drex", 0x1000);
}

static void S5L8730_drex_class_init(ObjectClass *klass, void *data)
{

}

static const TypeInfo ipod_nano5g_drex_info = {
    .name          = TYPE_IPOD_NANO5G_DREX,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodNano5GDREXState),
    .instance_init = S5L8730_drex_init,
    .class_init    = S5L8730_drex_class_init,
};

static void ipod_nano5g_machine_types(void)
{
    type_register_static(&ipod_nano5g_drex_info);
}

type_init(ipod_nano5g_machine_types)

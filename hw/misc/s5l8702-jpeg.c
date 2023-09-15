#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/s5l8702-jpeg.h"

#define JPEG_UNK1   0x60000

static uint64_t s5l8702_jpeg_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    const S5L8702JpegState *s = S5L8702_JPEG(opaque);
    uint32_t r = 0;

    switch (offset) {
    case JPEG_UNK1:
        r = 0xFFFFFFFF;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read (offset 0x%04x)\n",
                      __func__, (uint32_t) offset);
    }

    return r;
}

static void s5l8702_jpeg_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    S5L8702JpegState *s = S5L8702_JPEG(opaque);

    switch (offset) {
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write (offset 0x%04x, value 0x%08x)\n",
                      __func__, (uint32_t) offset, (uint32_t) val);
    }
}

static const MemoryRegionOps s5l8702_jpeg_ops = {
    .read = s5l8702_jpeg_read,
    .write = s5l8702_jpeg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_jpeg_reset(DeviceState *dev)
{
    S5L8702JpegState *s = S5L8702_JPEG(dev);

    printf("s5l8702_jpeg_reset\n");

    /* Reset registers */

}

static void s5l8702_jpeg_init(Object *obj)
{
    S5L8702JpegState *s = S5L8702_JPEG(obj);

    printf("s5l8702_jpeg_init\n");

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_jpeg_ops, s, TYPE_S5L8702_JPEG, S5L8702_JPEG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void s5l8702_jpeg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = s5l8702_jpeg_reset;
}

static const TypeInfo s5l8702_jpeg_types[] = {
    {
        .name = TYPE_S5L8702_JPEG,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_init = s5l8702_jpeg_init,
        .instance_size = sizeof(S5L8702JpegState),
        .class_init = s5l8702_jpeg_class_init,
    },
};
DEFINE_TYPES(s5l8702_jpeg_types);

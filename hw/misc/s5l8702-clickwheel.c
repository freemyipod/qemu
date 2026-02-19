#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/s5l8702-clickwheel.h"
#include "trace.h"
#include "crypto/hash.h"

#define WHEEL00  0x00
#define WHEEL04  0x04
#define WHEEL08  0x08
#define WHEEL0C  0x0C
#define WHEEL10  0x10
#define WHEELINT 0x14
#define WHEELRX  0x18
#define WHEELTX  0x1C

static void clickwheel_reset(S5L8702ClickwheelState *s) {
    memset(s->buffer, 0, sizeof(s->buffer));
    s->buffer_len = 0;
    memset(s->outbuf, 0, sizeof(s->outbuf));
    s->hash_computed = false;
}

static uint64_t s5l8702_clickwheel_read(void *opaque, hwaddr offset, unsigned size) {
    S5L8702ClickwheelState *s = S5L8702_CLICKWHEEL(opaque);

    switch (offset) {
        default:
            qemu_log_mask(LOG_UNIMP, "%s: unimplemented read (offset 0x%04x)\n", __func__, (uint32_t) offset);
    }

    return 0;
}

static void s5l8702_clickwheel_write(void *opaque, hwaddr offset, uint64_t val, unsigned size) {
    S5L8702ClickwheelState *s = S5L8702_CLICKWHEEL(opaque);

    switch (offset) {
        default:
            qemu_log_mask(LOG_UNIMP, "%s: unimplemented write (offset 0x%04x)\n", __func__, (uint32_t) offset);
    }
}

static const MemoryRegionOps s5l8702_clickwheel_ops = {
        .read = s5l8702_clickwheel_read,
        .write = s5l8702_clickwheel_write,
        .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_clickwheel_reset(DeviceState *dev) {
    S5L8702ClickwheelState *s = S5L8702_CLICKWHEEL(dev);

    trace_s5l8702_clickwheel_reset();

    s->config = 0;
    memset(s->inbuf, 0, sizeof(s->inbuf));
    clickwheel_reset(s);
}

static void s5l8702_clickwheel_init(Object *obj) {
    S5L8702ClickwheelState *s = S5L8702_CLICKWHEEL(obj);

    trace_s5l8702_clickwheel_init();

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_clickwheel_ops, s, TYPE_S5L8702_CLICKWHEEL, S5L8702_CLICKWHEEL_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void s5l8702_clickwheel_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = s5l8702_clickwheel_reset;
}

static const TypeInfo s5l8702_clickwheel_types[] = {
        {
                .name = TYPE_S5L8702_CLICKWHEEL,
                .parent = TYPE_SYS_BUS_DEVICE,
                .instance_init = s5l8702_clickwheel_init,
                .instance_size = sizeof(S5L8702ClickwheelState),
                .class_init = s5l8702_clickwheel_class_init,
        },
};
DEFINE_TYPES(s5l8702_clickwheel_types);

/*
 * S5L8702 pseudo-random number generator.
 *
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/s5l8702-prng.h"
#include "trace.h"

// The generator is a plain xorshift32. This is probably sufficient.
static uint32_t s5l8702_prng_next(S5L8702PrngState *s) {
    uint32_t x = s->state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s->state = x;

    return x;
}

static uint64_t s5l8702_prng_read(void *opaque, hwaddr offset, unsigned size) {
    S5L8702PrngState *s = S5L8702_PRNG(opaque);
    uint32_t value;

    switch (offset) {
    case S5L8702_PRNG_STATUS:
        return S5L8702_PRNG_STATUS_READY;

    case S5L8702_PRNG_DATA:
        value = s5l8702_prng_next(s);
        trace_s5l8702_prng_data(value);
        return value;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }
}

static void s5l8702_prng_write(void *opaque, hwaddr offset, uint64_t value, unsigned size) {
    S5L8702PrngState *s = S5L8702_PRNG(opaque);

    switch (offset) {
    case S5L8702_PRNG_STATUS:
        break;

    case S5L8702_PRNG_SEED:
        // A zero seed would wedge xorshift, so fall back to the default.
        s->state = value ? (uint32_t)value : S5L8702_PRNG_DEFAULT_SEED;
        trace_s5l8702_prng_seed((uint32_t)value);
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write at offset 0x%" HWADDR_PRIx " value 0x%" PRIx64 "\n", __func__, offset, value);
        break;
    }
}

static const MemoryRegionOps s5l8702_prng_ops = {
    .read = s5l8702_prng_read,
    .write = s5l8702_prng_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_prng_reset(DeviceState *dev) {
    S5L8702PrngState *s = S5L8702_PRNG(dev);

    s->state = S5L8702_PRNG_DEFAULT_SEED;
}

static void s5l8702_prng_init(Object *obj) {
    S5L8702PrngState *s = S5L8702_PRNG(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &s5l8702_prng_ops, s, TYPE_S5L8702_PRNG, S5L8702_PRNG_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_s5l8702_prng = {
    .name = TYPE_S5L8702_PRNG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(state, S5L8702PrngState),
        VMSTATE_END_OF_LIST()
    },
};

static void s5l8702_prng_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = s5l8702_prng_reset;
    dc->vmsd = &vmstate_s5l8702_prng;
}

static const TypeInfo s5l8702_prng_types[] = {
    {
        .name          = TYPE_S5L8702_PRNG,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(S5L8702PrngState),
        .instance_init = s5l8702_prng_init,
        .class_init    = s5l8702_prng_class_init,
    },
};
DEFINE_TYPES(s5l8702_prng_types);

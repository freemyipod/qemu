#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/s5l8702-sha.h"

#define SHA1CONFIG  0x0000
#define SHA1RESET   0x0004
#define SHA1OUT     0x0020
#define SHA1IN      0x0040

static uint64_t swapLong(void *X) {
    uint64_t x = (uint64_t) X;
    x = (x & 0x00000000FFFFFFFF) << 32 | (x & 0xFFFFFFFF00000000) >> 32;
    x = (x & 0x0000FFFF0000FFFF) << 16 | (x & 0xFFFF0000FFFF0000) >> 16;
    x = (x & 0x00FF00FF00FF00FF) << 8 | (x & 0xFF00FF00FF00FF00) >> 8;
    return x;
}

static void sha1_reset(S5L8702ShaState *s) {
    memset(s->buffer, 0, sizeof(s->buffer));
    s->buffer_len = 0;
    memset(s->outbuf, 0, sizeof(s->outbuf));
    s->hash_computed = false;
}

static uint64_t s5l8702_sha_read(void *opaque, hwaddr offset,
                                 unsigned size) {
    S5L8702ShaState *s = S5L8702_SHA(opaque);

    switch (offset) {
        case SHA1CONFIG:
            return s->config;
        case SHA1RESET:
            return 0;
        case SHA1OUT ... SHA1OUT + 16 * 4:
            if (!s->hash_computed) {
                // lazy compute the final hash by inspecting the last eight bytes of the buffer, which contains the length of the input data.
                uint64_t data_length = swapLong(((uint64_t *) s->buffer)[s->buffer_len / 8 - 1]) / 8;
                printf("SHA1: data length %d\n", data_length);

                SHA_CTX ctx;
                SHA1_Init(&ctx);
                SHA1_Update(&ctx, s->buffer, data_length);
                SHA1_Final(s->outbuf, &ctx);
                s->hash_computed = true;
            }

            return *(uint32_t *) &s->outbuf[offset - SHA1OUT];
        default:
            qemu_log_mask(LOG_UNIMP, "%s: unimplemented read (offset 0x%04x)\n",
                          __func__, (uint32_t) offset);
    }

    return 0;
}

static void s5l8702_sha_write(void *opaque, hwaddr offset,
                              uint64_t val, unsigned size) {
    S5L8702ShaState *s = S5L8702_SHA(opaque);

    switch (offset) {
        case SHA1CONFIG:
            printf("SHA config: %08x\n", val);
            if (val == 0x2 || val == 0xa) {
                if (val == 0x2) {
                    sha1_reset(s);
                }

                memcpy(s->buffer + s->buffer_len, s->inbuf, sizeof(s->inbuf));
                s->buffer_len += sizeof(s->inbuf);

                printf("Current length: %d\n", s->buffer_len);

                memset(s->inbuf, 0, sizeof(s->inbuf));

                s->hash_computed = false;
            } else {
                s->config = val;
            }
            break;
        case SHA1RESET:
            printf("SHA reset: %08x\n", val);
            sha1_reset(s);
            break;
        case SHA1IN ... SHA1IN + 16 * 4:
            printf("SHA hw buffer[%d]: %08x\n", (offset - SHA1IN) / 4, val);
            s->inbuf[(offset - SHA1IN) / 4] = val;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "%s: unimplemented write (offset 0x%04x)\n",
                          __func__, (uint32_t) offset);
    }
}

static const MemoryRegionOps s5l8702_sha_ops = {
        .read = s5l8702_sha_read,
        .write = s5l8702_sha_write,
        .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_sha_reset(DeviceState *dev) {
    S5L8702ShaState *s = S5L8702_SHA(dev);

    printf("s5l8702_sha_reset\n");

    s->config = 0;
    memset(s->inbuf, 0, sizeof(s->inbuf));
    sha1_reset(s);
}

static void s5l8702_sha_init(Object *obj) {
    S5L8702ShaState *s = S5L8702_SHA(obj);

    printf("s5l8702_sha_init\n");

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_sha_ops, s, TYPE_S5L8702_SHA, S5L8702_SHA_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void s5l8702_sha_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = s5l8702_sha_reset;
}

static const TypeInfo s5l8702_sha_types[] = {
        {
                .name = TYPE_S5L8702_SHA,
                .parent = TYPE_SYS_BUS_DEVICE,
                .instance_init = s5l8702_sha_init,
                .instance_size = sizeof(S5L8702ShaState),
                .class_init = s5l8702_sha_class_init,
        },
};
DEFINE_TYPES(s5l8702_sha_types);

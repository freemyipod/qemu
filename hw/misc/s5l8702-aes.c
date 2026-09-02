#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "crypto/aes.h"
#include "hw/misc/s5l8702-aes.h"
#include "trace.h"

#define REG_INDEX(offset) (offset / sizeof(uint32_t))

/* Largest transfer we will act on: the machine's DRAM. */
#define S5L8702_AES_MAX_XFER (32 * MiB)

/*
 * Stand-in for the SoC's fused UID key -- arbitrary, but stable, so that a
 * value the guest encrypts in one boot still decrypts in the next.
 */
static const uint8_t s5l8702_aes_uid_key[32] = {
    0x51, 0x8f, 0x2c, 0xd6, 0x4b, 0x1a, 0xe7, 0x03,
    0x9c, 0x22, 0xf5, 0x80, 0x37, 0xbe, 0x6d, 0x14,
    0xa8, 0x59, 0x0e, 0xc3, 0x72, 0xd1, 0x46, 0x9b,
    0x25, 0xfa, 0x83, 0x1c, 0x60, 0xd7, 0xb4, 0x2e,
};

static uint64_t s5l8702_aes_read(void *opaque, hwaddr offset,
                                 unsigned size) {
    const S5L8702AesState *s = S5L8702_AES(opaque);

    switch (offset) {
        case AES_STATUS:
            return s->status;
        default:
            qemu_log_mask(LOG_UNIMP, "%s: unimplemented read (offset 0x%04x)\n",
                          __func__, (uint32_t) offset);
    }

    return 0; //s->regs[idx];
}

static void s5l8702_aes_write(void *opaque, hwaddr offset,
                              uint64_t value, unsigned size) {
    S5L8702AesState *s = S5L8702_AES(opaque);

    trace_s5l8702_aes_write((uint32_t)offset, (uint32_t)value);

    switch (offset) {
        case AES_GO: {
            uint8_t *buf;
            uint32_t len = s->insize;

            /*
             * INSIZE is guest-programmed and the guest does get it wrong: osos
             * fires GO with INSIZE holding what looks like a peripheral address
             * (0x38da0000) and IN/OUTADDR = 7. Honouring that means a ~900 MB
             * host allocation plus a DMA read of the same length from unassigned
             * space, repeatedly, until the host OOM-kills QEMU. Nothing the
             * machine can legitimately encrypt is larger than its DRAM, so
             * refuse anything that big and leave the engine idle-but-finished
             * rather than letting the guest size a host allocation.
             */
            if (len > S5L8702_AES_MAX_XFER) {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: refusing %u-byte transfer (max %u)\n",
                              __func__, len, S5L8702_AES_MAX_XFER);
                trace_s5l8702_aes_xfer_too_large(len, s->inaddr);
                s->status = 0xf;
                break;
            }
            uint8_t iv[AES_BLOCK_SIZE];
            bool decrypt = (s->keylen == 14);
            AES_KEY key;
            bool have_key = false;

            switch (s->keytype) {
            case AESGID:
                /*
                 * The GID key is fused into the SoC and unknown to us. Every
                 * image the guest hands to this engine under it was decrypted
                 * on real hardware before being fed to QEMU (see
                 * docs/system/arm/ipod-nano3g.rst), so the engine is an
                 * identity transform in both directions.
                 */
                trace_s5l8702_aes_no_support("GID");
                break;
            case AESUID:
                /*
                 * The UID key is per-device and equally unknown, but nothing
                 * outside this machine ever produced UID-encrypted data: the
                 * guest encrypts and later decrypts its own. A fixed stand-in
                 * key is therefore self-consistent, which is what the guest
                 * actually depends on.
                 */
                AES_set_decrypt_key(s5l8702_aes_uid_key,
                                    sizeof(s5l8702_aes_uid_key) * 8,
                                    &s->decryptKey);
                AES_set_encrypt_key(s5l8702_aes_uid_key,
                                    sizeof(s5l8702_aes_uid_key) * 8, &key);
                have_key = true;
                break;
            case AESCustom:
                AES_set_decrypt_key((uint8_t *) s->custkey, 0x20 * 8,
                                    &s->decryptKey);
                AES_set_encrypt_key((uint8_t *) s->custkey, 0x20 * 8, &key);
                have_key = true;
                break;
            }

            buf = g_malloc(len);
            cpu_physical_memory_read(s->inaddr, buf, len);
            memcpy(iv, s->ivec, sizeof(iv));

            if (have_key) {
                /*
                 * CBC over the whole transfer. A short tail (the engine is fed
                 * whole blocks in practice) is passed through untouched rather
                 * than read past.
                 */
                for (uint32_t off = 0; off + AES_BLOCK_SIZE <= len;
                     off += AES_BLOCK_SIZE) {
                    uint8_t *blk = buf + off;

                    if (decrypt) {
                        uint8_t prev[AES_BLOCK_SIZE];

                        memcpy(prev, blk, AES_BLOCK_SIZE);
                        AES_decrypt(blk, blk, &s->decryptKey);
                        for (uint32_t i = 0; i < AES_BLOCK_SIZE; i++) {
                            blk[i] ^= iv[i];
                        }
                        memcpy(iv, prev, AES_BLOCK_SIZE);
                    } else {
                        for (uint32_t i = 0; i < AES_BLOCK_SIZE; i++) {
                            blk[i] ^= iv[i];
                        }
                        AES_encrypt(blk, blk, &key);
                        memcpy(iv, blk, AES_BLOCK_SIZE);
                    }
                }
            }

            trace_s5l8702_aes_operation(decrypt ? "decrypted" : "encrypted",
                                        len, s->inaddr, s->outaddr);

            cpu_physical_memory_write(s->outaddr, buf, len);
            g_free(buf);

            memset(s->custkey, 0, sizeof(s->custkey));
            memset(s->ivec, 0, sizeof(s->ivec));
            s->outsize = s->insize;
            s->status = 0xf;
            break;
        }
        case AES_KEYLEN:
            s->keylen = value;
            break;
        case AES_INADDR:
            s->inaddr = value;
            break;
        case AES_INSIZE:
            s->insize = value;
            break;
        case AES_OUTSIZE:
            s->outsize = value;
            break;
        case AES_OUTADDR:
            s->outaddr = value;
            break;
        case AES_TYPE:
            s->keytype = value;
            break;
        case AES_KEY_REG ... ((AES_KEY_REG + AES_KEYSIZE) - 1): {
            uint8_t idx = (offset - AES_KEY_REG) / 4;
            s->custkey[idx] = value;
            break;
        }
        case AES_IV_REG ... ((AES_IV_REG + AES_IVSIZE) - 1): {
            uint8_t idx = (offset - AES_IV_REG) / 4;
            s->ivec[idx] = value;
            break;
        }
        default:
            // fprintf(stderr, "%s: UNMAPPED AES_ADDR @ offset 0x%08x - 0x%08x\n", __FUNCTION__, offset, value);
            trace_s5l8702_aes_write((uint32_t)offset, (uint32_t)value);
            break;
    }
}

static const MemoryRegionOps s5l8702_aes_ops = {
        .read = s5l8702_aes_read,
        .write = s5l8702_aes_write,
        .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_aes_reset(DeviceState *dev) {
    S5L8702AesState *s = S5L8702_AES(dev);

    trace_s5l8702_aes_reset();

    /* Reset registers */
    // memset(s->regs, 0, sizeof(s->regs));

    /* Set default values for registers */
    s->status = 0x0000000F; // simulate AES finished
}

static void s5l8702_aes_init(Object *obj) {
    S5L8702AesState *s = S5L8702_AES(obj);

    trace_s5l8702_aes_init();

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_aes_ops, s, TYPE_S5L8702_AES, S5L8702_AES_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void s5l8702_aes_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = s5l8702_aes_reset;
}

static const TypeInfo s5l8702_aes_types[] = {
        {
                .name = TYPE_S5L8702_AES,
                .parent = TYPE_SYS_BUS_DEVICE,
                .instance_init = s5l8702_aes_init,
                .instance_size = sizeof(S5L8702AesState),
                .class_init = s5l8702_aes_class_init,
        },
};
DEFINE_TYPES(s5l8702_aes_types);

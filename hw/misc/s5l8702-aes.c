#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/s5l8702-aes.h"

#define REG_INDEX(offset) (offset / sizeof(uint32_t))

static uint64_t s5l8702_aes_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    const S5L8702AesState *s = S5L8702_AES(opaque);
    const uint32_t idx = REG_INDEX(offset);

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
                                   uint64_t value, unsigned size)
{
    S5L8702AesState *s = S5L8702_AES(opaque);
    const uint32_t idx = REG_INDEX(offset);

    uint8_t *inbuf;
    uint8_t *buf;
    static uint8_t keylenop = 0;

    fprintf(stderr, "%s: offset 0x%08x value 0x%08x\n", __FUNCTION__, offset, value);

    switch(offset) {
        case AES_GO:
            inbuf = (uint8_t *)malloc(s->insize);
            cpu_physical_memory_read((s->inaddr), inbuf, s->insize);

            switch(s->keytype) {
                    case AESGID:    
                        fprintf(stderr, "%s: No support for GID key\n", __func__);
                        break;
                    case AESUID:
                        fprintf(stderr, "%s: No support for UID key\n", __func__);
                        // AES_set_decrypt_key(key_uid, sizeof(key_uid) * 8, &s->decryptKey);
                        break;
                    case AESCustom:
                        AES_set_decrypt_key((uint8_t *)s->custkey, 0x20 * 8, &s->decryptKey);
                        break;
            }

            buf = (uint8_t *) malloc(s->insize);

            // ignore the GID key because it's assumed anything encrypted with this key has been decrypted prior to emulation
            if(s->keytype != 0x01) AES_cbc_encrypt(inbuf, buf, s->insize, &s->decryptKey, (uint8_t *)s->ivec, s->operation);

            printf("AES: %s %d bytes from 0x%08x to 0x%08x\n", s->operation == AES_DECRYPT ? "decrypted" : "encrypted", s->insize, s->inaddr, s->outaddr);

            cpu_physical_memory_write((s->outaddr), buf, s->insize);
            memset(s->custkey, 0, 0x20);
            memset(s->ivec, 0, 0x10);
            free(inbuf);
            free(buf);
            keylenop = 0;
            s->outsize = s->insize;
            s->status = 0xf;
            break;
        case AES_KEYLEN:
            if(keylenop == 1) {
                s->operation = value;
            }
            keylenop++;
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
        case AES_KEY_REG ... ((AES_KEY_REG + AES_KEYSIZE) - 1):
            {
                uint8_t idx = (offset - AES_KEY_REG) / 4;
                s->custkey[idx] |= value;
                break;
            }
        case AES_IV_REG ... ((AES_IV_REG + AES_IVSIZE) -1 ):
            {
                uint8_t idx = (offset - AES_IV_REG) / 4;
                s->ivec[idx] |= value;
                break;
            }
        default:
            //fprintf(stderr, "%s: UNMAPPED AES_ADDR @ offset 0x%08x - 0x%08x\n", __FUNCTION__, offset, value);
            break;
    }
}

static const MemoryRegionOps s5l8702_aes_ops = {
    .read = s5l8702_aes_read,
    .write = s5l8702_aes_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    // .valid = {
    //     .min_access_size = 4,
    //     .max_access_size = 4,
    // },
    // .impl.min_access_size = 4,
};

static void s5l8702_aes_reset(DeviceState *dev)
{
    S5L8702AesState *s = S5L8702_AES(dev);

    printf("s5l8702_aes_reset\n");

    /* Reset registers */
    // memset(s->regs, 0, sizeof(s->regs));

    /* Set default values for registers */
    s->status = 0x0000000F; // simulate AES finished
}

static void s5l8702_aes_init(Object *obj)
{
    S5L8702AesState *s = S5L8702_AES(obj);

    printf("s5l8702_aes_init\n");

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_aes_ops, s, TYPE_S5L8702_AES, S5L8702_AES_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void s5l8702_aes_class_init(ObjectClass *klass, void *data)
{
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

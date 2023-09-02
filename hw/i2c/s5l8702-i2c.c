#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/i2c/s5l8702-i2c.h"

#define S5L8702_I2C_IICCON      0x00
#define S5L8702_I2C_IICSTAT     0x04
#define S5L8702_I2C_IICADD      0x08
#define S5L8702_I2C_IICDS       0x0C
#define S5L8702_I2C_IIUNK10     0x10
#define S5L8702_I2C_IIUNK14     0x14
#define S5L8702_I2C_IIUNK18     0x18
#define S5L8702_I2C_IICSTAT2    0x20

/* IICCON */
#define S5L8702_I2C_IICCON_ACK_GEN      BIT(7)
#define S5L8702_I2C_IICCON_CKSEL        BIT(6)
// #define S5L8702_I2C_IICCON_INT_EN       BIT(5)
#define S5L8702_I2C_IICCON_IRQ          BIT(4)
#define S5L8702_I2C_IICCON_CK_REG(x)    ((x) & 0x7)
#define S5L8702_I2C_IICCON_CK_REG_MASK  0x7

/* IICSTAT */
#define S5L8702_I2C_IICSTAT_MODE_SEL(x)     (((x) & 0x3) << 6)
#define S5L8702_I2C_IICSTAT_MODE_SEL_MASK   (0x3 << 6)
#define S5L8702_I2C_IICSTAT_MODE_BB         BIT(5)
#define S5L8702_I2C_IICSTAT_MODE_SOE        BIT(4)
#define S5L8702_I2C_IICSTAT_MODE_LBA        BIT(3)
#define S5L8702_I2C_IICSTAT_MODE_AAS        BIT(2)
#define S5L8702_I2C_IICSTAT_MODE_ADDR_ZERO  BIT(1)
#define S5L8702_I2C_IICSTAT_MODE_LRB        BIT(0)

/* IICADD */
#define S5L8702_I2C_IICADD_S_ADDR(x)    (((x) & 0x7F) << 1)
#define S5L8702_I2C_IICADD_S_ADDR_MASK  (0x7F << 1)

/* IICDS */
#define S5L8702_I2C_IICDS_DATA(x)   (((x) & 0xFF) << 0)

static uint64_t s5l8702_i2c_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    const S5L8702I2cState *s = S5L8702_I2C(opaque);
    uint32_t r = 0;

    switch (offset) {
    case S5L8702_I2C_IICCON:
        r = s->iiccon;
        printf("s5l8702_i2c_read: IICCON = 0x%08x\n", r);
        break;
    case S5L8702_I2C_IICSTAT:
        r = s->iicstat;
        r &= ~S5L8702_I2C_IICSTAT_MODE_BB; // The virtualized i2c peripheral is never busy 😎
        printf("s5l8702_i2c_read: IICSTAT = 0x%08x\n", r);
        break;
    case S5L8702_I2C_IICADD:
        r = s->iicadd;
        printf("s5l8702_i2c_read: IICADD = 0x%08x\n", r);
        break;
    case S5L8702_I2C_IICDS:
        r = s->iicds;
        printf("s5l8702_i2c_read: IICDS = 0x%08x\n", r);
        break;
    case S5L8702_I2C_IIUNK10:
        r = s->iicunk10;
        printf("s5l8702_i2c_read: IIUNK10 = 0x%08x\n", r);
        break;
    case S5L8702_I2C_IIUNK14:
        r = s->iicunk14;
        printf("s5l8702_i2c_read: IIUNK14 = 0x%08x\n", r);
        break;
    case S5L8702_I2C_IIUNK18:
        r = s->iicunk18;
        printf("s5l8702_i2c_read: IIUNK18 = 0x%08x\n", r);
        break;
    case S5L8702_I2C_IICSTAT2:
        r = s->iicstat2;
        printf("s5l8702_i2c_read: IICSTAT2 = 0x%08x\n", r);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read (offset 0x%04x)\n",
                      __func__, (uint32_t) offset);
    }

    return r;
}

static void s5l8702_i2c_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    S5L8702I2cState *s = S5L8702_I2C(opaque);

    switch (offset) {
    case S5L8702_I2C_IICCON:
        printf("s5l8702_i2c_write: IICCON = 0x%08x\n", (uint32_t) val);

        // print ack_gen, cksel, int_en, irq, ck_reg
        printf("s5l8702_i2c_write: IICCON: ACK_GEN = %d\n", (val & S5L8702_I2C_IICCON_ACK_GEN) >> 7);
        printf("s5l8702_i2c_write: IICCON: CKSEL = %d\n", (val & S5L8702_I2C_IICCON_CKSEL) >> 6);
        printf("s5l8702_i2c_write: IICCON: INT_EN = %d\n", (val & S5L8702_I2C_IICCON_IRQ) >> 4);
        printf("s5l8702_i2c_write: IICCON: CK_REG = %d\n", (val & S5L8702_I2C_IICCON_CK_REG_MASK) >> 0);

        if (val & S5L8702_I2C_IICCON_IRQ) {
            val &= ~S5L8702_I2C_IICCON_IRQ;

            if ((s->iicstat & 0xF0) == 0xF0) { // Resume TX
                printf("s5l8702_i2c_write: IICCON: resume tx\n");
                int ack = i2c_send(s->bus, (uint8_t) s->iicds);
                printf("s5l8702_i2c_write: IICCON: ack = %d\n", ack);
                if (ack) {
                    s->iicstat |= 1; // Set NACK signal
                } else {
                    s->iicstat &= ~1; // Clear NACK signal
                }
                val |= S5L8702_I2C_IICCON_IRQ; // Set IRQ signal
                s->iicstat2 |= BIT(8); // Set transfer done? CHECKME!
            }

            if ((s->iicstat & 0xF0) == 0xD0) { // Stop TX
                printf("s5l8702_i2c_write: IICCON: stop tx\n");
                i2c_end_transfer(s->bus);
                val |= S5L8702_I2C_IICCON_IRQ; // Set IRQ signal
                s->iicstat2 |= BIT(8); // Set transfer done? CHECKME!
            }

            if ((s->iicstat & 0xF0) == 0xB0) { // Resume RX
                printf("s5l8702_i2c_write: IICCON: resume rx\n");
                s->iicds = i2c_recv(s->bus);
                printf("s5l8702_i2c_write: IICCON: data = 0x%02x\n", s->iicds);
                s->iicstat &= ~1; // Clear NACK signal

                val |= S5L8702_I2C_IICCON_IRQ; // Set IRQ signal
                s->iicstat2 |= BIT(8); // Set transfer done? CHECKME!
            }

            if ((s->iicstat & 0xF0) == 0x90) { // Stop RX
                printf("s5l8702_i2c_write: IICCON: stop rx\n");
                i2c_end_transfer(s->bus);
                val |= S5L8702_I2C_IICCON_IRQ; // Set IRQ signal
                s->iicstat2 |= BIT(8); // Set transfer done? CHECKME!
            }
        }

        s->iiccon = (uint32_t) val;
        break;
    case S5L8702_I2C_IICSTAT:
        printf("s5l8702_i2c_write: IICSTAT = 0x%08x\n", (uint32_t) val);

        printf("s5l8702_i2c_write: IICSTAT: MODE_SEL = 0x%02x\n", (uint8_t) ((val & S5L8702_I2C_IICSTAT_MODE_SEL_MASK) >> 6));
        printf("s5l8702_i2c_write: IICSTAT: BB = %d\n", (val & S5L8702_I2C_IICSTAT_MODE_BB) >> 5);
        printf("s5l8702_i2c_write: IICSTAT: SOE = %d\n", (val & S5L8702_I2C_IICSTAT_MODE_SOE) >> 4);

        if ((val & 0xF0) == 0xF0) {
            printf("s5l8702_i2c_write: IICSTAT: start tx, slave address = 0x%02x\n", ((uint8_t) s->iicds) >> 1);
            int ack = i2c_start_send(s->bus, ((uint8_t) s->iicds) >> 1);
            printf("s5l8702_i2c_write: IICSTAT: ack = %d\n", ack);
            if (ack) {
                val |= S5L8702_I2C_IICSTAT_MODE_LRB; // Set NACK signal
            } else {
                val &= ~S5L8702_I2C_IICSTAT_MODE_LRB; // Clear NACK signal
            }
            s->iiccon |= S5L8702_I2C_IICCON_IRQ; // Set IRQ signal
            s->iicstat2 |= BIT(8); // Set transfer done? CHECKME!
        }
        
        if ((val & 0xF0) == 0xD0) {
            printf("s5l8702_i2c_write: IICSTAT: stop tx\n");
            s->iiccon |= S5L8702_I2C_IICCON_IRQ; // Set IRQ signal
            s->iicstat2 |= BIT(8); // Set transfer done? CHECKME!
        }

        if ((val & 0xF0) == 0xB0) {
            printf("s5l8702_i2c_write: IICSTAT: start rx, slave address = 0x%02x\n", ((uint8_t) s->iicds) >> 1);
            int ack = i2c_start_recv(s->bus, ((uint8_t) s->iicds) >> 1);
            printf("s5l8702_i2c_write: IICSTAT: ack = %d\n", ack);
            if (ack) {
                val |= 1; // Set NACK signal
            } else {
                val &= ~1; // Clear NACK signal
            }
            s->iiccon |= S5L8702_I2C_IICCON_IRQ; // Set IRQ signal
            s->iicstat2 |= BIT(8); // Set transfer done? CHECKME!
        }
        
        if ((val & 0xF0) == 0x90) {
            printf("s5l8702_i2c_write: IICSTAT: stop rx\n");
            s->iiccon |= S5L8702_I2C_IICCON_IRQ; // Set IRQ signal
            s->iicstat2 |= BIT(8); // Set transfer done? CHECKME!
        }

        // FIXME: LBA, AAS, ADDR_ZERO and LRB are read-only
        s->iicstat = (uint32_t) val;
        break;
    case S5L8702_I2C_IICADD:
        printf("s5l8702_i2c_write: IICADD = 0x%08x\n", (uint32_t) val);
        // TODO: Only writeable when serial output is disabled!
        s->iicadd = (uint32_t) val;
        break;
    case S5L8702_I2C_IICDS:
        printf("s5l8702_i2c_write: IICDS = 0x%08x\n", (uint32_t) val);
        s->iicds = (uint32_t) val;
        break;
    case S5L8702_I2C_IIUNK10:
        printf("s5l8702_i2c_write: IIUNK10 = 0x%08x\n", (uint32_t) val);
        s->iicunk10 = (uint32_t) val;
        break;
    case S5L8702_I2C_IIUNK14:
        printf("s5l8702_i2c_write: IIUNK14 = 0x%08x\n", (uint32_t) val);
        s->iicunk14 = (uint32_t) val;
        break;
    case S5L8702_I2C_IIUNK18:
        printf("s5l8702_i2c_write: IIUNK18 = 0x%08x\n", (uint32_t) val);
        s->iicunk18 = (uint32_t) val;
        break;
    case S5L8702_I2C_IICSTAT2:
        printf("s5l8702_i2c_write: IICSTAT2 = 0x%08x\n", (uint32_t) val);
        s->iicstat2 &= ~(uint8_t) val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write (offset 0x%04x, value 0x%08x)\n",
                      __func__, (uint32_t) offset, (uint32_t) val);
    }
}

static const MemoryRegionOps s5l8702_i2c_ops = {
    .read = s5l8702_i2c_read,
    .write = s5l8702_i2c_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_i2c_reset(DeviceState *dev)
{
    S5L8702I2cState *s = S5L8702_I2C(dev);

    printf("s5l8702_i2c_reset\n");

    /* Reset registers */
    s->iiccon = 0;
    s->iicstat = 0;
    s->iicadd = 0;
    s->iicds = 0;
    s->iicunk10 = 0;
    s->iicunk14 = 0;
    s->iicunk18 = 0;
    s->iicstat2 = 0;
}

static void s5l8702_i2c_init(Object *obj)
{
    S5L8702I2cState *s = S5L8702_I2C(obj);

    printf("s5l8702_i2c_init\n");

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_i2c_ops, s, TYPE_S5L8702_I2C, S5L8702_I2C_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    s->bus = i2c_init_bus(DEVICE(obj), "s5l8702-i2c");
}

static void s5l8702_i2c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = s5l8702_i2c_reset;
}

static const TypeInfo s5l8702_i2c_types[] = {
    {
        .name = TYPE_S5L8702_I2C,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_init = s5l8702_i2c_init,
        .instance_size = sizeof(S5L8702I2cState),
        .class_init = s5l8702_i2c_class_init,
    },
};
DEFINE_TYPES(s5l8702_i2c_types);

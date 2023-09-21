#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/ssi/s5l8702-spi.h"

#define SPICTRL     0x00
#define SPISETUP    0x04
#define SPISTATUS   0x08
#define SPIPIN      0x0c
#define SPITXDATA   0x10
#define SPIRXDATA   0x20
#define SPICLKDIV   0x30
#define SPIRXLIMIT  0x34

#define SPICTRL_CLRTX  BIT(2)
#define SPICTRL_CLRRX  BIT(3)

#define SPISETUP_UNK0   BIT(0) // Auto receive after send?
#define SPISETUP_UNK1   BIT(1)

#define SPISTATUS_TX_CNT_MASK   0xf0
#define SPISTATUS_TX_CNT        BIT(4)
#define SPISTATUS_RX_CNT_MASK   0xf00
#define SPISTATUS_RX_CNT        BIT(8)


static void s5l8702_spi_update_irq(S5L8702SpiState *s)
{
    // TODO
}

static uint64_t s5l8702_spi_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    S5L8702SpiState *s = S5L8702_SPI(opaque);
    uint32_t r = 0;

    switch (offset) {
    case SPICTRL:
        if (s->spictrl & 1) {
            s->spictrl &= ~BIT(1);
        } else {
            s->spictrl |= BIT(1);
        }
        r = s->spictrl;
        //printf("%s: SPICTRL: 0x%08x\n", __func__, r);
        break;
    case SPISTATUS:
        r = s->spistatus;
        if (s->spistatus & SPISTATUS_RX_CNT) {
            r |= 0x3e00;
        }
        if (s->spisetup & 1) {
            r |= 0x3e00;
        }
        //printf("%s: SPISTATUS: 0x%08x\n", __func__, r);
        break;
    case SPISETUP:
        r = s->spisetup;
        //printf("%s: SPISETUP: 0x%08x\n", __func__, r);
        break;
    case SPITXDATA:
        r = s->spitxdata;
        //printf("%s: SPITXDATA: 0x%08x\n", __func__, r);
        break;
    case SPIRXDATA:
        s->spistatus &= ~SPISTATUS_RX_CNT;
        if (s->spisetup & SPISETUP_UNK0) {
            s->spirxdata = ssi_transfer(s->spi, 0xFF);
        }
        r = s->spirxdata;
        //printf("%s: SPIRXDATA: 0x%08x\n", __func__, r);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read (offset 0x%04x)\n",
                      __func__, (uint32_t) offset);
        break;
    }

    s5l8702_spi_update_irq(s);

    return r;
}

static void s5l8702_spi_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    S5L8702SpiState *s = S5L8702_SPI(opaque);

    switch (offset) {
    case SPICTRL:
        //printf("%s: SPICTRL: 0x%08x\n", __func__, (uint32_t) val);
        s->spictrl = (uint32_t) val;
        if (s->spictrl & SPICTRL_CLRTX) {
            s->spictrl &= ~SPICTRL_CLRTX;
            //printf("%s: SPICTRL: clearing tx fifo...\n", __func__);
            s->spistatus &= ~SPISTATUS_TX_CNT;
        }
        if (s->spictrl & SPICTRL_CLRRX) {
            s->spictrl &= ~SPICTRL_CLRRX;
            //printf("%s: SPICTRL: clearing rx fifo...\n", __func__);
            s->spistatus &= ~SPISTATUS_RX_CNT;
        }
        break;
    case SPISETUP:
        //printf("%s: SPISETUP: 0x%08x\n", __func__, (uint32_t) val);
        s->spisetup = (uint32_t) val;
        break;
    case SPITXDATA:
        //printf("%s: SPITXDATA: 0x%08x\n", __func__, (uint32_t) val);
        s->spitxdata = (uint32_t) val;
        s->spirxdata = ssi_transfer(s->spi, s->spitxdata);
        s->spistatus |= SPISTATUS_RX_CNT;
        break;
    case SPIRXLIMIT:
        //printf("%s: SPIRXLIMIT: 0x%08x\n", __func__, (uint32_t) val);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write (offset 0x%04x, value 0x%08x)\n",
                      __func__, (uint32_t) offset, (uint32_t) val);
        break;
    }

    s5l8702_spi_update_irq(s);
}

static const MemoryRegionOps s5l8702_spi_ops = {
    .read = s5l8702_spi_read,
    .write = s5l8702_spi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_spi_reset(DeviceState *dev)
{
    S5L8702SpiState *s = S5L8702_SPI(dev);

    /* Set default values for registers */
    s->spictrl = 0;
    s->spisetup = 0;
    s->spistatus = 0;
    s->spitxdata = 0;
    s->spirxdata = 0;
}

static void s5l8702_spi_init(Object *obj)
{
    S5L8702SpiState *s = S5L8702_SPI(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_spi_ops, s, TYPE_S5L8702_SPI, S5L8702_SPI_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    s->spi = ssi_create_bus(DEVICE(obj), "spi");
}

static void s5l8702_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = s5l8702_spi_reset;
}

static const TypeInfo s5l8702_spi_types[] = {
    {
        .name = TYPE_S5L8702_SPI,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(S5L8702SpiState),
        .instance_init = s5l8702_spi_init,
        .class_init = s5l8702_spi_class_init,
    },
};
DEFINE_TYPES(s5l8702_spi_types);

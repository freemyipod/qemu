#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/ide/s5l8702-ata.h"

#define ATA_DMA_ADDR 0x38700088

#define ATA_CONTROL 0x0000
#define ATA_STATUS 0x0004
#define ATA_COMMAND 0x0008
#define ATA_SWRST 0x000c
#define ATA_IRQ 0x0010
#define ATA_IRQ_MASK 0x0014
#define ATA_CFG 0x0018
#define ATA_MDMA_TIME 0x0028
#define ATA_PIO_TIME 0x002c
#define ATA_UDMA_TIME 0x0030
#define ATA_XFR_NUM 0x0034
#define ATA_XFR_CNT 0x0038
#define ATA_TBUF_START 0x003c
#define ATA_TBUF_SIZE 0x0040
#define ATA_SBUF_START 0x0044
#define ATA_SBUF_SIZE 0x0048
#define ATA_CADR_TBUF 0x004c
#define ATA_CADR_SBUF 0x0050
#define ATA_PIO_DTR 0x0054
#define ATA_PIO_FED 0x0058
#define ATA_PIO_SCR 0x005c
#define ATA_PIO_LLR 0x0060
#define ATA_PIO_LMR 0x0064
#define ATA_PIO_LHR 0x0068
#define ATA_PIO_DVR 0x006c
#define ATA_PIO_CSD 0x0070
#define ATA_PIO_DAD 0x0074
#define ATA_PIO_READY 0x0078
#define ATA_PIO_RDATA 0x007c
#define ATA_BUS_FIFO_STATUS 0x0080
#define ATA_FIFO_STATUS 0x0084

/* ATA_CONTROL */
#define CLK_DOWN_READY          BIT(1)
#define ATA_ENABLE              BIT(0)

/* ATA_STATUS */
#define ATADEV_CBLID             BIT(5)
#define ATADEV_IRQ               BIT(4)
#define ATADEV_IORDY             BIT(3)
#define ATADEV_DMAREQ            BIT(2)
#define ATADEV_XFR_STATE         BIT(0)
#define ATADEV_XFR_STATE_MASK    0x3

/* ATA_COMMAND */
#define XFR_COMMAND             BIT(0)
#define XFR_COMMAND_MASK        0x3

/* ATA_SWRST */
#define ATA_SWRSTN                BIT(0)

/* ATA_IRQ */
#define SBUF_EMPTY_INT          BIT(4)
#define TBUF_FULL_INT           BIT(3)
#define ATADEV_IRQ_INT          BIT(2)
#define UDMA_HOLD_INT           BIT(1)
#define XFR_DONE_INT            BIT(0)

/* ATA_IRQ_MASK */
#define MASK_SBUT_EMPTY_INT     BIT(4)
#define MASK_TBUF_FULL_INT      BIT(3)
#define MASK_ATADEV_IRQ_INT     BIT(2)
#define MASK_UDMA_HOLD_INT      BIT(1)
#define MASK_XFR_DONE_INT       BIT(0)

/* ATA_CFG */
#define UDMA_AUTO_MODE  BIT(9)
#define SBUF_FULL_MODE  BIT(8)
#define SBUF_EMPTY_MODE BIT(8)
#define TBUF_FULL_MODE  BIT(7)
#define BYTE_SWAP       BIT(6)
#define ATADEV_IRQ_AL   BIT(5)
#define DMA_DIR         BIT(4)
#define ATA_CLASS       BIT(2)
#define ATA_CLASS_MASK  0xc
#define ATA_IORDY_EN    BIT(1)
#define ATA_RST         BIT(0)

/* ATA_PIO_READY */
#define DEV_ACC_READY   BIT(1)
#define PIO_DATA_READY  BIT(0)

static uint64_t s5l8702_ata_read(void *opaque, hwaddr offset,
                                 unsigned size)
{
    const S5L8702AtaState *s = S5L8702_ATA(opaque);
    uint32_t r = 0;

    switch (offset) {
        case ATA_CONTROL:
            r = s->ata_control;
            printf("%s: ATA_CONTROL: 0x%08x\n", __func__, r);
            break;
        case ATA_STATUS:
            r = s->ata_status;
            printf("%s: ATA_STATUS: 0x%08x\n", __func__, r);
            break;
        case ATA_COMMAND:
            r = s->ata_command;
            printf("%s: ATA_COMMAND: 0x%08x\n", __func__, r);
            break;
        case ATA_SWRST:
            r = s->ata_swrst;
            printf("%s: ATA_SWRST: 0x%08x\n", __func__, r);
            break;
        case ATA_IRQ:
//            r = s->ata_irq;
            r |= SBUF_EMPTY_MODE;
            r |= TBUF_FULL_MODE;
            r |= ATADEV_IRQ_AL;
            r |= UDMA_HOLD_INT;
            r |= XFR_DONE_INT;
            printf("%s: ATA_IRQ: 0x%08x\n", __func__, r);
            break;
        case ATA_IRQ_MASK:
            r = s->ata_irq_mask;
            printf("%s: ATA_IRQ_MASK: 0x%08x\n", __func__, r);
            break;
        case ATA_CFG:
            r = s->ata_cfg;
            printf("%s: ATA_CFG: 0x%08x\n", __func__, r);
            break;
        case ATA_MDMA_TIME:
            r = s->ata_mdma_time;
            printf("%s: ATA_MDMA_TIME: 0x%08x\n", __func__, r);
            break;
        case ATA_PIO_TIME:
            r = s->ata_pio_time;
            printf("%s: ATA_PIO_TIME: 0x%08x\n", __func__, r);
            break;
        case ATA_UDMA_TIME:
            r = s->ata_udma_time;
            printf("%s: ATA_UDMA_TIME: 0x%08x\n", __func__, r);
            break;
        case ATA_XFR_NUM:
            r = s->ata_xfr_num;
            printf("%s: ATA_XFR_NUM: 0x%08x\n", __func__, r);
            break;
        case ATA_XFR_CNT:
            r = s->ata_xfr_cnt;
            printf("%s: ATA_XFR_CNT: 0x%08x\n", __func__, r);
            break;
        case ATA_TBUF_START:
            r = s->ata_tbuf_start;
            printf("%s: ATA_TBUF_START: 0x%08x\n", __func__, r);
            break;
        case ATA_TBUF_SIZE:
            r = s->ata_tbuf_size;
            printf("%s: ATA_TBUF_SIZE: 0x%08x\n", __func__, r);
            break;
        case ATA_SBUF_START:
            r = s->ata_sbuf_start;
            printf("%s: ATA_SBUF_START: 0x%08x\n", __func__, r);
            break;
        case ATA_SBUF_SIZE:
            r = s->ata_sbuf_size;
            printf("%s: ATA_SBUF_SIZE: 0x%08x\n", __func__, r);
            break;
        case ATA_CADR_TBUF:
            r = s->ata_cadr_tbuf;
            printf("%s: ATA_CADR_TBUF: 0x%08x\n", __func__, r);
            break;
        case ATA_CADR_SBUF:
            r = s->ata_cadr_sbuf;
            printf("%s: ATA_CADR_SBUF: 0x%08x\n", __func__, r);
            break;
        case ATA_PIO_DTR:
            r = s->ata_pio_dtr;
            printf("%s: ATA_PIO_DTR: 0x%08x\n", __func__, r);
            break;
        case ATA_PIO_FED:
            r = s->ata_pio_fed;
            printf("%s: ATA_PIO_FED: 0x%08x\n", __func__, r);
            break;
        case ATA_PIO_SCR:
            r = s->ata_pio_scr;
            printf("%s: ATA_PIO_SCR: 0x%08x\n", __func__, r);
            break;
        case ATA_PIO_LLR:
            r = s->ata_pio_llr;
            printf("%s: ATA_PIO_LLR: 0x%08x\n", __func__, r);
            break;
        case ATA_PIO_LMR:
            r = s->ata_pio_lmr;
            printf("%s: ATA_PIO_LMR: 0x%08x\n", __func__, r);
            break;
        case ATA_PIO_LHR:
            r = s->ata_pio_lhr;
            printf("%s: ATA_PIO_LHR: 0x%08x\n", __func__, r);
            break;
        case ATA_PIO_DVR:
            r = s->ata_pio_dvr;
            printf("%s: ATA_PIO_DVR: 0x%08x\n", __func__, r);
            break;
        case ATA_PIO_CSD:
            r = s->ata_pio_csd;
            printf("%s: ATA_PIO_CSD: 0x%08x\n", __func__, r);
            break;
        case ATA_PIO_DAD:
//            r = s->ata_pio_dad;
            r = 0x04;
            printf("%s: ATA_PIO_DAD: 0x%08x\n", __func__, r);
            break;
        case ATA_PIO_READY:
//            r = s->ata_pio_ready;
            r |= DEV_ACC_READY;
            r |= PIO_DATA_READY;
            printf("%s: ATA_PIO_READY: 0x%08x\n", __func__, r);
            break;
        case ATA_PIO_RDATA:
            r = s->ata_pio_rdata;
            switch (s->ata_pio_csd) {
                case 0xec: // WIN_IDENTIFY: ask drive to identify itself
                    printf("%s: ATA_PIO_RDATA: WIN_IDENTIFY\n", __func__);
                    r = 0x4000;
                    break;
            }
            printf("%s: ATA_PIO_RDATA: 0x%08x\n", __func__, r);
            break;
        case ATA_BUS_FIFO_STATUS:
            r = s->ata_bus_fifo_status;
            printf("%s: ATA_BUS_FIFO_STATUS: 0x%08x\n", __func__, r);
            break;
        case ATA_FIFO_STATUS:
            r = s->ata_fifo_status;
            printf("%s: ATA_FIFO_STATUS: 0x%08x\n", __func__, r);
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "%s: unimplemented read (offset 0x%04x)\n",
                          __func__, (uint32_t) offset);
    }

    return r;
}

static void s5l8702_ata_write(void *opaque, hwaddr offset,
                              uint64_t val, unsigned size)
{
    S5L8702AtaState *s = S5L8702_ATA(opaque);

    switch (offset) {
        case ATA_CONTROL:
            printf("%s: ATA_CONTROL: 0x%08x\n", __func__, (uint32_t) val);

            if ((val & ATA_ENABLE) != (s->ata_control & ATA_ENABLE)) {
                if (val & ATA_ENABLE) {
                    printf("%s: ATA_CONTROL: enabling ATA\n", __func__);
                } else {
                    printf("%s: ATA_CONTROL: disabling ATA\n", __func__);
                }
            }

            s->ata_control = (uint32_t) val;
            break;
        case ATA_COMMAND:
            printf("%s: ATA_COMMAND: 0x%08x\n", __func__, (uint32_t) val);

            if ((val & XFR_COMMAND_MASK) != (s->ata_command & XFR_COMMAND_MASK)) {
                switch ((val & XFR_COMMAND_MASK) >> XFR_COMMAND) {
                    case 0:
                        printf("%s: ATA_COMMAND: command stop\n", __func__);
                        break;
                    case 1:
                        printf("%s: ATA_COMMAND: command start\n", __func__);
                        break;
                    case 2:
                        printf("%s: ATA_COMMAND: command abort\n", __func__);
                        break;
                    case 3:
                        printf("%s: ATA_COMMAND: command continue\n", __func__);
                        break;
                }
            }

            s->ata_command = (uint32_t) val;
            break;
        case ATA_SWRST:
            printf("%s: ATA_SWRST: 0x%08x\n", __func__, (uint32_t) val);

            if ((val & ATA_SWRSTN) != (s->ata_swrst & ATA_SWRSTN)) {
                if (val & ATA_SWRSTN) {
                    printf("%s: ATA_SWRST: reset asserted\n", __func__);
                } else {
                    printf("%s: ATA_SWRST: reset deasserted\n", __func__);
                }
            }

            s->ata_swrst = (uint32_t) val;
            break;
        case ATA_IRQ:
            printf("%s: ATA_IRQ: 0x%08x\n", __func__, (uint32_t) val);
            if (val & SBUF_EMPTY_INT) {
                printf("%s: ATA_IRQ: Clearing SBUF_EMPTY_INT\n", __func__);
            }
            if (val & TBUF_FULL_INT) {
                printf("%s: ATA_IRQ: Clearing TBUF_FULL_INT\n", __func__);
            }
            if (val & ATADEV_IRQ_INT) {
                printf("%s: ATA_IRQ: Clearing ATADEV_IRQ_INT\n", __func__);
            }
            if (val & UDMA_HOLD_INT) {
                printf("%s: ATA_IRQ: Clearing UDMA_HOLD_INT\n", __func__);
            }
            if (val & XFR_DONE_INT) {
                printf("%s: ATA_IRQ: Clearing XFR_DONE_INT\n", __func__);
            }
            s->ata_irq &= ~(uint32_t) val;
            break;
        case ATA_IRQ_MASK:
            printf("%s: ATA_IRQ_MASK: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_irq_mask = (uint32_t) val;
            break;
        case ATA_CFG:
            printf("%s: ATA_CFG: 0x%08x\n", __func__, (uint32_t) val);

            if (val & UDMA_AUTO_MODE) {
                printf("%s: ATA_CFG: UDMA_AUTO_MODE set\n", __func__);
            }
            if (val & SBUF_FULL_MODE) {
                printf("%s: ATA_CFG: SBUF_FULL_MODE set\n", __func__);
            }
            if (val & SBUF_EMPTY_MODE) {
                printf("%s: ATA_CFG: SBUF_EMPTY_MODE set\n", __func__);
            }
            if (val & TBUF_FULL_MODE) {
                printf("%s: ATA_CFG: TBUF_FULL_MODE set\n", __func__);
            }
            if (val & BYTE_SWAP) {
                printf("%s: ATA_CFG: BYTE_SWAP set\n", __func__);
            }
            if (val & ATADEV_IRQ_AL) {
                printf("%s: ATA_CFG: ATADEV_IRQ_AL set\n", __func__);
            }
            if (val & DMA_DIR) {
                printf("%s: ATA_CFG: DMA_DIR set\n", __func__);
            }
            switch ((val & ATA_CLASS_MASK) >> ATA_CLASS) {
                case 0:
                    printf("%s: ATA_CFG: ATA_CLASS: PIO\n", __func__);
                    break;
                case 1:
                    printf("%s: ATA_CFG: ATA_CLASS: PIO DMA\n", __func__);
                    break;
                case 2:
                case 3:
                    printf("%s: ATA_CFG: ATA_CLASS: UDMA\n", __func__);
                    break;
            }
            if (val & ATA_IORDY_EN) {
                printf("%s: ATA_CFG: ATA_IORDY_EN set\n", __func__);
            }
            if (val & ATA_RST) {
                printf("%s: ATA_CFG: ATA_RST set\n", __func__);
            }
            s->ata_cfg = (uint32_t) val;
            break;
        case ATA_MDMA_TIME:
            printf("%s: ATA_MDMA_TIME: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_mdma_time = (uint32_t) val;
            break;
        case ATA_PIO_TIME:
            printf("%s: ATA_PIO_TIME: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_pio_time = (uint32_t) val;
            break;
        case ATA_UDMA_TIME:
            printf("%s: ATA_UDMA_TIME: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_udma_time = (uint32_t) val;
            break;
        case ATA_XFR_NUM:
            printf("%s: ATA_XFR_NUM: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_xfr_num = (uint32_t) val;
            break;
        case ATA_XFR_CNT:
            printf("%s: ATA_XFR_CNT: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_xfr_cnt = (uint32_t) val;
            break;
        case ATA_TBUF_START:
            printf("%s: ATA_TBUF_START: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_tbuf_start = (uint32_t) val;
            break;
        case ATA_TBUF_SIZE:
            printf("%s: ATA_TBUF_SIZE: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_tbuf_size = (uint32_t) val;
            break;
        case ATA_SBUF_START:
            printf("%s: ATA_SBUF_START: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_sbuf_start = (uint32_t) val;
            break;
        case ATA_SBUF_SIZE:
            printf("%s: ATA_SBUF_SIZE: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_sbuf_size = (uint32_t) val;
            break;
        case ATA_CADR_TBUF:
            printf("%s: ATA_CADR_TBUF: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_cadr_tbuf = (uint32_t) val;
            break;
        case ATA_CADR_SBUF:
            printf("%s: ATA_CADR_SBUF: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_cadr_sbuf = (uint32_t) val;
            break;
        case ATA_PIO_DTR:
            printf("%s: ATA_PIO_DTR: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_pio_dtr = (uint32_t) val;
            break;
        case ATA_PIO_FED:
            printf("%s: ATA_PIO_FED: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_pio_fed = (uint32_t) val;
            break;
        case ATA_PIO_SCR:
            printf("%s: ATA_PIO_SCR: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_pio_scr = (uint32_t) val;
            break;
        case ATA_PIO_LLR:
            printf("%s: ATA_PIO_LLR: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_pio_llr = (uint32_t) val;
            break;
        case ATA_PIO_LMR:
            printf("%s: ATA_PIO_LMR: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_pio_lmr = (uint32_t) val;
            break;
        case ATA_PIO_LHR:
            printf("%s: ATA_PIO_LHR: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_pio_lhr = (uint32_t) val;
            break;
        case ATA_PIO_DVR:
            printf("%s: ATA_PIO_DVR: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_pio_dvr = (uint32_t) val;
            break;
        case ATA_PIO_CSD:
            printf("%s: ATA_PIO_CSD: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_pio_csd = (uint32_t) val;
            break;
        case ATA_PIO_DAD:
            printf("%s: ATA_PIO_DAD: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_pio_dad = (uint32_t) val;
            break;
        case ATA_PIO_READY:
            printf("%s: ATA_PIO_READY: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_pio_ready = (uint32_t) val;
            break;
        case ATA_PIO_RDATA:
            printf("%s: ATA_PIO_RDATA: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_pio_rdata = (uint32_t) val;
            break;
        case ATA_BUS_FIFO_STATUS:
            printf("%s: ATA_BUS_FIFO_STATUS: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_bus_fifo_status = (uint32_t) val;
            break;
        case ATA_FIFO_STATUS:
            printf("%s: ATA_FIFO_STATUS: 0x%08x\n", __func__, (uint32_t) val);
            s->ata_fifo_status = (uint32_t) val;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "%s: unimplemented write (offset 0x%04x, value 0x%08x)\n",
                          __func__, (uint32_t) offset, (uint32_t) val);
    }
}

static const MemoryRegionOps s5l8702_ata_ops = {
        .read = s5l8702_ata_read,
        .write = s5l8702_ata_write,
        .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_ata_reset(DeviceState *dev)
{
    S5L8702AtaState *s = S5L8702_ATA(dev);

    printf("s5l8702_ata_reset\n");

    /* Reset registers */
    s->ata_control = 0;
    s->ata_status = 0;
    s->ata_command = 0;
    s->ata_swrst = 0;
    s->ata_irq = 0;
    s->ata_irq_mask = 0;
    s->ata_cfg = 0;
    s->ata_mdma_time = 0;
    s->ata_pio_time = 0;
    s->ata_udma_time = 0;
    s->ata_xfr_num = 0;
    s->ata_xfr_cnt = 0;
    s->ata_tbuf_start = 0;
    s->ata_tbuf_size = 0;
    s->ata_sbuf_start = 0;
    s->ata_sbuf_size = 0;
    s->ata_cadr_tbuf = 0;
    s->ata_cadr_sbuf = 0;
    s->ata_pio_dtr = 0;
    s->ata_pio_fed = 0;
    s->ata_pio_scr = 0;
    s->ata_pio_llr = 0;
    s->ata_pio_lmr = 0;
    s->ata_pio_lhr = 0;
    s->ata_pio_dvr = 0;
    s->ata_pio_csd = 0;
    s->ata_pio_dad = 0;
    s->ata_pio_ready = 0;
    s->ata_pio_rdata = 0;
    s->ata_bus_fifo_status = 0;
    s->ata_fifo_status = 0;
}

static void s5l8702_ata_init(Object *obj)
{
    S5L8702AtaState *s = S5L8702_ATA(obj);

    printf("s5l8702_ata_init\n");

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_ata_ops, s, TYPE_S5L8702_ATA, S5L8702_ATA_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void s5l8702_ata_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = s5l8702_ata_reset;
}

static const TypeInfo s5l8702_ata_types[] = {
        {
                .name = TYPE_S5L8702_ATA,
                .parent = TYPE_SYS_BUS_DEVICE,
                .instance_init = s5l8702_ata_init,
                .instance_size = sizeof(S5L8702AtaState),
                .class_init = s5l8702_ata_class_init,
        },
};
DEFINE_TYPES(s5l8702_ata_types);

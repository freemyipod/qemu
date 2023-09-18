#ifndef HW_MISC_S5L8702_ATA_H
#define HW_MISC_S5L8702_ATA_H

#include "qom/object.h"
#include "hw/sysbus.h"

#define TYPE_S5L8702_ATA    "s5l8702-ata"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702AtaState, S5L8702_ATA)

#define S5L8702_ATA_SIZE    0x00100000

struct S5L8702AtaState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t ata_control;
    uint32_t ata_status;
    uint32_t ata_command;
    uint32_t ata_swrst;
    uint32_t ata_irq;
    uint32_t ata_irq_mask;
    uint32_t ata_cfg;
    uint32_t ata_mdma_time;
    uint32_t ata_pio_time;
    uint32_t ata_udma_time;
    uint32_t ata_xfr_num;
    uint32_t ata_xfr_cnt;
    uint32_t ata_tbuf_start;
    uint32_t ata_tbuf_size;
    uint32_t ata_sbuf_start;
    uint32_t ata_sbuf_size;
    uint32_t ata_cadr_tbuf;
    uint32_t ata_cadr_sbuf;
    uint32_t ata_pio_dtr;
    uint32_t ata_pio_fed;
    uint32_t ata_pio_scr;
    uint32_t ata_pio_llr;
    uint32_t ata_pio_lmr;
    uint32_t ata_pio_lhr;
    uint32_t ata_pio_dvr;
    uint32_t ata_pio_csd;
    uint32_t ata_pio_dad;
    uint32_t ata_pio_ready;
    uint32_t ata_pio_rdata;
    uint32_t ata_bus_fifo_status;
    uint32_t ata_fifo_status;
};

#endif /* HW_MISC_S5L8702_ATA_H */

#ifndef HW_MISC_S5L8702_LCD_H
#define HW_MISC_S5L8702_LCD_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "ui/console.h"
#include "qemu/osdep.h"
#include "qemu/fifo8.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/irq.h"

#define TYPE_S5L8702_LCD    "s5l8702-lcd"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702LcdState, S5L8702_LCD)

#define S5L8702_LCD_SIZE    0x00100000

struct S5L8702LcdState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    uint32_t config;
    uint32_t wcmd;
    uint32_t status;
    uint32_t phtime;
    uint32_t wdata;

    uint8_t address_latches;
    uint16_t sc, ec, sp, ep;


    QemuConsole *con;

    bool invalidate;

    MemoryRegion *sysmem;
    AddressSpace *nsas;
    MemoryRegionSection fbsection;
    qemu_irq irq;

    Fifo8* dbuff_buf;

    uint32_t lcd_config;
    uint32_t lcd_wcmd;
    uint32_t lcd_rcmd;
    uint32_t lcd_rdata;
    uint32_t lcd_dbuff;
    uint32_t lcd_intcon;
    uint32_t lcd_status;
    uint32_t lcd_phtime;
    uint32_t lcd_wdata;

    uint64_t* lcd_regs; // internal registers in case we ever need to access them in the future

    uint16_t* framebuffer;
    uint64_t memcnt;

    uint32_t unknown1;
    uint32_t unknown2;

    uint32_t wnd_con;

    uint32_t vid_con0;
    uint32_t vid_con1;

    uint32_t vidt_con0;
    uint32_t vidt_con1;
    uint32_t vidt_con2;
    uint32_t vidt_con3;

    uint32_t w1_hspan;
    uint32_t w1_framebuffer_base;
    uint32_t w1_display_resolution_info;
    uint32_t w1_display_depth_info;
    uint32_t w1_qlen;

    uint32_t w2_hspan;
    uint32_t w2_framebuffer_base;
    uint32_t w2_display_resolution_info;
    uint32_t w2_display_depth_info;
    uint32_t w2_qlen;

    QEMUTimer *refresh_timer;
};

#endif /* HW_MISC_S5L8702_LCD_H */

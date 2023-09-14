#ifndef HW_MISC_S5L8702_LCD_H
#define HW_MISC_S5L8702_LCD_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "ui/console.h"

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

    QemuConsole *con;

    bool invalidate;
};

#endif /* HW_MISC_S5L8702_LCD_H */

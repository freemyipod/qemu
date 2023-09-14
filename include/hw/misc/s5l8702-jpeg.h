#ifndef HW_MISC_S5L8702_JPEG_H
#define HW_MISC_S5L8702_JPEG_H

#include "qom/object.h"
#include "hw/sysbus.h"

#define TYPE_S5L8702_JPEG    "s5l8702-jpeg"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702JpegState, S5L8702_JPEG)

#define S5L8702_JPEG_SIZE    0x00100000

struct S5L8702JpegState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
};

#endif /* HW_MISC_S5L8702_JPEG_H */

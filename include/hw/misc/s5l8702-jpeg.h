#ifndef HW_MISC_S5L8702_JPEG_H
#define HW_MISC_S5L8702_JPEG_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "qemu/fifo8.h"
#include "qemu/log.h"
#include "hw/platform-bus.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "exec/hwaddr.h"
#include "exec/memory.h"

#define TYPE_S5L8702_JPEG    "s5l8702-jpeg"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702JpegState, S5L8702_JPEG)

#define S5L8702_JPEG_SIZE    0x00100000

#define M_PI           3.14159265358979323846  /* pi */

#define JPEG_REG_QTABLE1 0x41200
#define JPEG_REG_QTABLE2 0x41300
#define JPEG_QTABLE_LEN  0xFF

#define JPEG_REG_CTRL     0x5000C

#define JPEG_REG_COEFF_BLOCKS 0x60018
#define JPEG_REG_OUT_YPLANE   0x6002c
#define JPEG_REG_OUT_CBPLANE  0x6003c
#define JPEG_REG_OUT_CRPLANE  0x6004c

typedef struct {
    uint32_t coeff[0x40];
} Block;

typedef struct {
    Block lum[4];
    Block chromb;
    Block chromr;
} EncodedMCU;

typedef struct {
    double yplane[16][16];
    double crplane[16][16];
    double cbplane[16][16];
} DecodedMCU;

struct S5L8702JpegState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    AddressSpace *nsas;
    MemoryRegion *sysmem;
    qemu_irq irq;

    uint32_t regs[0x396fffff - 0x39600000];
    uint32_t qtable1[64];
    uint32_t qtable2[64];
};

#endif /* HW_MISC_S5L8702_JPEG_H */

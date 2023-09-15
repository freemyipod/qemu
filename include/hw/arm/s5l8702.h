#ifndef HW_ARM_S5L8702_H
#define HW_ARM_S5L8702_H

#include "hw/sysbus.h"
#include "hw/clock.h"
#include "target/arm/cpu.h"
#include "hw/intc/arm_gic.h"
#include "hw/intc/pl192.h"
#include "hw/misc/s5l8702-clk.h"
#include "hw/misc/s5l8702-aes.h"
#include "hw/misc/s5l8702-sha.h"
#include "hw/timer/s5l8702-timer.h"
#include "hw/gpio/s5l8702-gpio.h"
#include "hw/ssi/s5l8702-spi.h"
#include "hw/i2c/s5l8702-i2c.h"
#include "hw/misc/s5l8702-lcd.h"
#include "hw/misc/s5l8702-jpeg.h"
#include "hw/dma/pl080.h"

#define TYPE_S5L8702    "s5l8702"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702State, S5L8702)

#define S5L8702_BOOTROM_BASE_ADDR   0x20000000
#define S5L8702_BOOTROM_SIZE        0x00010000  /* 64 KB */

#define S5L8702_DRAM_BASE_ADDR      0x08000000
#define S5L8702_DRAM_SIZE           0x04000000  /* 64 MB */

#define S5L8702_IRAM_BASE_ADDR      0x22000000
#define S5L8702_IRAM_SIZE           0x00040000  /* 256 KB */

#define S5L8702_IRAM0_BASE_ADDR     0x22000000
#define S5L8702_IRAM0_SIZE          0x00020000  /* 128 KB */
#define S5L8702_IRAM1_BASE_ADDR     0x22020000
#define S5L8702_IRAM1_SIZE          0x00020000  /* 128 KB */

#define S5L8702_VIC_BASE_ADDR       0x38E00000
#define S5L8702_CLK_BASE_ADDR       0x3C500000

#define S5L8702_BASE_BOOT_ADDR      0x0

struct S5L8702State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    ARMCPU cpu;
    MemoryRegion brom;          // S5L8702_BOOTROM_BASE_ADDR
    MemoryRegion brom_alias;    // S5L8702_BASE_BOOT_ADDR
    MemoryRegion iram0;         // S5L8702_IRAM0_BASE_ADDR
    MemoryRegion iram1;         // S5L8702_IRAM1_BASE_ADDR
    PL192State vic[2];
    Clock pclk;
    Clock eclk;
    Clock extclk0;
    Clock extclk1;
    S5L8702ClkState clk;
    S5L8702AesState aes;
    S5L8702ShaState sha;
    S5L8702GpioState gpio;
    S5L8702SpiState spi[3];
    S5L8702I2cState i2c[2];
    S5L8702TimerCtrlState timer;
    S5L8702LcdState lcd;
    S5L8702JpegState jpeg;
    PL080State dma[2];
};

#endif /* HW_ARM_S5L8702_H */

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
#include "hw/misc/s5l8702-chipid.h"
#include "hw/misc/s5l8702-prng.h"
#include "hw/misc/s5l8702-miu.h"
#include "hw/misc/s5l8702-clickwheel.h"
#include "hw/misc/s5l8702-nand.h"
#include "hw/misc/s5l8702-nand-ecc.h"
#include "hw/misc/s5l8702-usbotg.h"
#include "hw/misc/s5l8702-usbphy.h"
#include "hw/misc/s5l8702-sysic.h"
#include "hw/dma/pl080.h"
#include "hw/ide/s5l8702-ata.h"
#include "hw/misc/s5l8702-buscon.h"
#include "hw/misc/s5l8702-clcd.h"

#define TYPE_S5L8702    "s5l8702"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702State, S5L8702)

#define S5L8702_BOOTROM_BASE_ADDR   0x20000000
#define S5L8702_BOOTROM_SIZE        0x00010000  /* 64 KB */

#define S5L8702_DRAM_BASE_ADDR      0x08000000

#define S5L8702_IRAM_BASE_ADDR      0x22000000
#define S5L8702_IRAM_SIZE           0x00040000  /* 256 KB */

#define S5L8702_IRAM0_BASE_ADDR     0x22000000
#define S5L8702_IRAM0_SIZE          0x00020000  /* 128 KB */
#define S5L8702_IRAM1_BASE_ADDR     0x22020000
#define S5L8702_IRAM1_SIZE          0x00020000  /* 128 KB */

#define S5L8702_VIC0_MEM_BASE 0x38E00000
#define S5L8702_VIC1_MEM_BASE 0x38E01000

#define S5L8702_DMA0_BASE 0x38200000
#define S5L8702_DMA1_BASE 0x39900000
#define S5L8702_IRQ_DMAC0 16
#define S5L8702_IRQ_DMAC1 17

#define S5L8702_UART0_MEM_BASE 0x3CC00000
#define S5L8702_UART1_MEM_BASE 0x3CC04000
#define S5L8702_UART2_MEM_BASE 0x3CC08000
#define S5L8702_UART3_MEM_BASE 0x3CC0C000
#define S5L8702_UART4_MEM_BASE 0x3CC10000
#define S5L8702_IRQ_UART0 24
#define S5L8702_IRQ_UART1 25
#define S5L8702_IRQ_UART2 26
#define S5L8702_IRQ_UART3 27
#define S5L8702_IRQ_UART4 28

#define S5L8702_IRQ_USBOTG 19  /* VIC0 IRQ 19 (0x13): firmware writes 1<<19 to VIC0 INTENABLE */

#define S5L8702_BASE_BOOT_ADDR      0x0

struct S5L8702State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    ARMCPU cpu;
    qemu_irq **irq;
    MemoryRegion brom;          // S5L8702_BOOTROM_BASE_ADDR
    MemoryRegion brom_alias;    // S5L8702_BASE_BOOT_ADDR
    MemoryRegion iram0_alias;   // S5L8702_BASE_BOOT_ADDR, when remapped
    MemoryRegion iram0;         // S5L8702_IRAM0_BASE_ADDR
    MemoryRegion iram1;         // S5L8702_IRAM1_BASE_ADDR
    PL192State* vic0;
    PL192State* vic1;
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
    S5L8702AtaState ata;
    S5L8702ClickwheelState clickwheel;
    S5L8702ChipIDState chipid;
    S5L8702PrngState prng;
    S5L8702NandState nand;
    S5L8702NandEccState nand_ecc;
    S5L8702MiuState miu;
    S5L8702UsbOtgState usbotg;
    S5L8702UsbPhyState usbphy;
    S5L8702SysICState sysic;
    S5L8702BusConState buscon;
    S5L8702ClcdState clcd;
    DeviceState* uart[4];
};

#endif /* HW_ARM_S5L8702_H */

#ifndef HW_GPIO_S5L8702_GPIO_H
#define HW_GPIO_S5L8702_GPIO_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include "hw/irq.h"

#define TYPE_S5L8702_GPIO   "s5l8702-gpio"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702GpioState, S5L8702_GPIO)

#define S5L8702_GPIO_BASE   0x3CF00000
#define S5L8702_GPIO_SIZE   0x00100000

#define S5L8702_GPIO_PINS   128
#define S5L8702_GPIO_PORTS  (S5L8702_GPIO_PINS / 8)

#define S5L8702_GPIO_PORT(n)    (n / 8)
#define S5L8702_GPIO_PIN(n)     (n % 8)

#define GPIO_N_GROUPS  16
#define GPIO_N_PINS 0x20

struct S5L8702GpioState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq output[S5L8702_GPIO_PINS];

    uint8_t pcon[S5L8702_GPIO_PORTS];
    uint8_t pdat[S5L8702_GPIO_PORTS];
    uint8_t puna[S5L8702_GPIO_PORTS];
    uint8_t punb[S5L8702_GPIO_PORTS];
    uint8_t punc[S5L8702_GPIO_PORTS];
    uint8_t gpiocmd;

    uint8_t gpio_pin_state[GPIO_N_GROUPS][GPIO_N_PINS];

    uint32_t clickwheel_rx_buf;
    uint32_t clickwheel_tx_buf;
    uint8_t clickwheel_clk;
    uint8_t clickwheel_bit_to_send;
    uint8_t clickwheel_skip_cycle;

    uint8_t clickwheel_select_pressed;
    uint8_t clickwheel_menu_pressed;
    uint8_t clickwheel_play_pressed;
    uint8_t clickwheel_prev_pressed;
    uint8_t clickwheel_next_pressed;
};

#endif /* HW_GPIO_S5L8702_GPIO_H */

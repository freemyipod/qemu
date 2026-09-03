#ifndef HW_MISC_D1671_H
#define HW_MISC_D1671_H

#include "qom/object.h"
#include "hw/i2c/i2c.h"

#define TYPE_D1671    "d1671"
OBJECT_DECLARE_SIMPLE_TYPE(D1671State, D1671)

/* STATUSA register bits */
#define D1671_STATUSA_USB_DETECTED       (1 << 3)
#define D1671_STATUSA_FIREWIRE_DETECTED  (1 << 4)
#define D1671_STATUSA_ACCESSORY_DETECTED (1 << 5)
#define D1671_STATUSA_CHARGER_DETECTED   (1 << 6)

/* STATUSB register bits */
#define D1671_STATUSB_HOLD_SWITCH_OFF        (1 << 0)

struct D1671State {
    /*< private >*/
    I2CSlave i2c;

    /*< public >*/    
    bool has_word;
    uint8_t word;

    uint8_t regs[0xFF];
    uint16_t adc[16];

    bool usb_power;
};

#endif /* HW_MISC_D1671_H */

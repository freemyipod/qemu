#ifndef HW_MISC_PCF5063X_H
#define HW_MISC_PCF5063X_H

#include "qom/object.h"
#include "hw/i2c/i2c.h"

#define TYPE_PCF5063X    "pcf5063x"
OBJECT_DECLARE_SIMPLE_TYPE(Pcf5063xState, PCF5063X)

struct Pcf5063xState {
    /*< private >*/
    I2CSlave i2c;

    /*< public >*/    
    bool has_word;
    uint8_t word;

    uint8_t regs[0xFF];
};

#endif /* HW_MISC_PCF5063X_H */

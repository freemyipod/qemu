#ifndef HW_MISC_PCF5063X_H
#define HW_MISC_PCF5063X_H

#include "qom/object.h"
#include "hw/i2c/i2c.h"

#define TYPE_PCF5063X    "pcf5063x"
OBJECT_DECLARE_SIMPLE_TYPE(Pcf5063xState, PCF5063X)

#define PCF5063X_NUM_REGS   64

struct Pcf5063xState {
    /*< private >*/
    I2CSlave i2c;

    /*< public >*/
    enum i2c_event event;
    uint8_t cmd;
    uint8_t regs[PCF5063X_NUM_REGS];
};

#endif /* HW_MISC_PCF5063X_H */

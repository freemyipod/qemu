#ifndef HW_MISC_D1671_H
#define HW_MISC_D1671_H

#include "qom/object.h"
#include "hw/i2c/i2c.h"

#define TYPE_D1671    "d1671"
OBJECT_DECLARE_SIMPLE_TYPE(D1671State, D1671)

struct D1671State {
    /*< private >*/
    I2CSlave i2c;

    /*< public >*/    
    bool has_word;
    uint8_t word;

    uint8_t regs[0xFF];
};

#endif /* HW_MISC_D1671_H */

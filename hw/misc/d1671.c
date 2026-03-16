#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/d1671.h"
#include "trace.h"

#define D1671_STATUSA 0x05
#define D1671_STATUSB 0x06
#define D1671_SYSCTRLA 0x08
#define D1671_CHCTL 0x21
#define D1671_ADC_CTRL 0x30
#define D1671_ADC_DATA_LSB 0x31
#define D1671_ADC_DATA_MSB 0x32

static uint8_t d1671_read(D1671State *s, uint8_t addr) {
    uint8_t r = s->regs[addr];
    const char* register_name = "UNKNOWN";

    switch (addr) {
    case D1671_STATUSA:
        r = D1671_STATUSA_USB_DETECTED | D1671_STATUSA_FIREWIRE_DETECTED | D1671_STATUSA_ACCESSORY_DETECTED;
        register_name = "STATUSA";
        break;
    case D1671_STATUSB:
        r = D1671_STATUSB_HOLD_SWITCH;
        register_name = "STATUSB";
        break;
    case D1671_SYSCTRLA:
        r = s->regs[D1671_SYSCTRLA] | 0x01; // Set Bit 0 (0x01) = Power Good
        register_name = "SYSCTRLA";
        break;
    case D1671_CHCTL:
        // Fast charging enabled
        r = s->regs[D1671_CHCTL] | 0x01;
        register_name = "CHCTL";
        break;
    // not totally sure how the ADC works
    case D1671_ADC_CTRL:
        r = s->regs[D1671_ADC_CTRL];
        register_name = "ADC_CTRL";
        break;
    case D1671_ADC_DATA_LSB:
        register_name = "ADC_DATA_LSB";
        r = 0x00;
        break;
    case D1671_ADC_DATA_MSB:
        register_name = "ADC_DATA_MSB";
        r = 0x80;
        break;
    case 0x33:
        register_name = "ADC_DATA?";
        r = s->adc[s->regs[D1671_ADC_CTRL] & 0x0F];
        break;
    }

    trace_d1671_read(register_name, addr, r);
    return r;
}

static void d1671_write(D1671State *s, uint8_t addr, uint8_t data){
    trace_d1671_write(addr, data);
    s->regs[addr] = data;
}

static int d1671_event(I2CSlave *slave, enum i2c_event event) {
    D1671State *s = D1671(slave);
    if (event == I2C_START_SEND) {
        s->has_word = false;
    }
    return 0;
}

static uint8_t d1671_recv(I2CSlave *slave) {
    D1671State *s = D1671(slave);
    uint8_t r = 0;

    r = d1671_read(s, s->word);
    s->word++;
    
    return r;
}

static int d1671_send(I2CSlave *slave, uint8_t data) {
    D1671State *s = D1671(slave);
    
    if (!s->has_word) {
        s->has_word = true;
        s->word = data;
    } else {
        d1671_write(s, s->word, data);
        s->word++;
    }

    return 0;
}

static void d1671_reset(DeviceState *dev) {
    D1671State *s = D1671(dev);
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->adc, 0, sizeof(s->adc));
    s->adc[12] = 0x99; // Battery Voltage
}

static void d1671_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *isc = I2C_SLAVE_CLASS(klass);

    dc->reset = d1671_reset;
    isc->event = d1671_event;
    isc->recv = d1671_recv;
    isc->send = d1671_send;
}

static const TypeInfo d1671_types[] = {
    {
        .name = TYPE_D1671,
        .parent = TYPE_I2C_SLAVE,
        .instance_size = sizeof(D1671State),
        .class_init = d1671_class_init,
    },
};
DEFINE_TYPES(d1671_types);

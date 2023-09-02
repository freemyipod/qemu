#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/pcf5063x.h"

#define PCF5063X_VERSION    0x00
#define PCF5063X_VARIANT    0x01
#define PCF5063X_INT1       0x02
#define PCF5063X_INT2       0x03
#define PCF5063X_INT3       0x04
#define PCF5063X_INT4       0x05
#define PCF5063X_INT5       0x06
#define PCF5063X_INT1MASK   0x07
#define PCF5063X_INT2MASK   0x08
#define PCF5063X_INT3MASK   0x09
#define PCF5063X_INT4MASK   0x0A
#define PCF5063X_INT5MASK   0x0B
#define PCF5063X_OOCSHDWN   0x0C
#define PCF5063X_OOCWAKE    0x0D
#define PCF5063X_OOCTIM1    0x0E
#define PCF5063X_OOCTIM2    0x0F
#define PCF5063X_OOCMODE    0x10
#define PCF5063X_OOCCTL     0x11
#define PCF5063X_OOCSTAT    0x12
#define PCF5063X_GPIOCTL    0x13
#define PCF5063X_GPIO1CFG   0x14
#define PCF5063X_GPIO2CFG   0x15
#define PCF5063X_GPIO3CFG   0x16
#define PCF5063X_GPOCFG     0x17
#define PCF5063X_BVMCTL     0x18
#define PCF5063X_SVMCTL     0x19
#define PCF5063X_AUTOOUT    0x1A
#define PCF5063X_AUTOENA    0x1B
#define PCF5063X_AUTOCTL    0x1C
#define PCF5063X_AUTOMXC    0x1D
#define PCF5063X_DOWN1OUT   0x1E
#define PCF5063X_DOWN1ENA   0x1F
#define PCF5063X_DOWN1CTL   0x20
#define PCF5063X_DOWN1MXC   0x21
#define PCF5063X_DOWN2OUT   0x22
#define PCF5063X_DOWN2ENA   0x23
#define PCF5063X_DOWN2CTL   0x24
#define PCF5063X_DOWN2MXC   0x25
#define PCF5063X_MEMLDOOUT  0x26
#define PCF5063X_MEMLDOENA  0x27
#define PCF5063X_LEDOUT     0x28
#define PCF5063X_LEDENA     0x29
#define PCF5063X_LEDCTL     0x2A
#define PCF5063X_LEDDIM     0x2B
#define PCF5063X_LDO1OUT    0x2D
#define PCF5063X_LDO1ENA    0x2E
#define PCF5063X_LDO2OUT    0x2F
#define PCF5063X_LDO2ENA    0x30
#define PCF5063X_LDO3OUT    0x31
#define PCF5063X_LDO3ENA    0x32
#define PCF5063X_LDO4OUT    0x33
#define PCF5063X_LDO4ENA    0x34
#define PCF5063X_LDO5OUT    0x35
#define PCF5063X_LDO5ENA    0x36
#define PCF5063X_LDO6OUT    0x37
#define PCF5063X_LDO6ENA    0x38
#define PCF5063X_HCLDOOUT   0x39
#define PCF5063X_HCLDOENA   0x3A
#define PCF5063X_STBYCTL1   0x3B
#define PCF5063X_STBYCTL2   0x3C
#define PCF5063X_DEBPF1     0x3D
#define PCF5063X_DEBPF2     0x3E
#define PCF5063X_DEBPF3     0x3F
#define PCF5063X_HCLDOOVL   0x40
#define PCF5063X_DCDCSTAT   0x41
#define PCF5063X_LDOSTAT    0x42
#define PCF5063X_MBCC1      0x43
#define PCF5063X_MBCC2      0x44
#define PCF5063X_MBCC3      0x45
#define PCF5063X_MBCC4      0x46
#define PCF5063X_MBCC5      0x47
#define PCF5063X_MBCC6      0x48
#define PCF5063X_MBCC7      0x49
#define PCF5063X_MBCC8      0x4A
#define PCF5063X_MBCS1      0x4B
#define PCF5063X_MBCS2      0x4C
#define PCF5063X_MBCS3      0x4D
#define PCF5063X_BBCCTL     0x4E
#define PCF5063X_ALMGAIN    0x4F
#define PCF5063X_ALMDATA    0x50
#define PCF5063X_ADCC3      0x52
#define PCF5063X_ADCC2      0x53
#define PCF5063X_ADCC1      0x54
#define PCF5063X_ADCS1      0x55
#define PCF5063X_ADCS2      0x56
#define PCF5063X_ADCS3      0x57
#define PCF5063X_RTCSC      0x59
#define PCF5063X_RTCMN      0x5A
#define PCF5063X_RTCHR      0x5B
#define PCF5063X_RTCWD      0x5C
#define PCF5063X_RTCDT      0x5D
#define PCF5063X_RTCMT      0x5E
#define PCF5063X_RTCYR      0x5F
#define PCF5063X_RTCSCA     0x60
#define PCF5063X_RTCMNA     0x61
#define PCF5063X_RTCHRA     0x62
#define PCF5063X_RTCWDA     0x63
#define PCF5063X_RTCDTA     0x64
#define PCF5063X_RTCMTA     0x65
#define PCF5063X_RTCYRA     0x66
#define PCF5063X_MEMBYTE0   0x67
#define PCF5063X_MEMBYTE1   0x68
#define PCF5063X_MEMBYTE2   0x69
#define PCF5063X_MEMBYTE3   0x6A
#define PCF5063X_MEMBYTE4   0x6B
#define PCF5063X_MEMBYTE5   0x6C
#define PCF5063X_MEMBYTE6   0x6D
#define PCF5063X_MEMBYTE7   0x6E
#define PCF5063X_DCDCPFM    0x84

/* undocummented PMU registers */
#define PCF50635_INT6       0x85
#define PCF50635_INT6M      0x86
#define PCF50635_GPIOSTAT   0x87

static uint8_t pcf5063x_read(Pcf5063xState *s, uint8_t addr)
{
    uint8_t r = 0;

    switch (addr) {
        // The following registers are read at the startup, INTX are just to clear them
    case PCF5063X_GPIO3CFG:
    case PCF5063X_OOCSHDWN:
    case PCF5063X_INT1:
    case PCF5063X_INT2:
    case PCF5063X_INT3:
    case PCF5063X_INT4:
    case PCF5063X_INT5:
    case PCF50635_INT6:
    default:
        printf("pcf5063x_read: unknown addr %02x\n", addr);
        r = s->regs[addr];
        break;
    }

    printf("pcf5063x_read: reading addr %02x: %02x\n", addr, r);

    return r;
}

static void pcf5063x_write(Pcf5063xState *s, uint8_t addr, uint8_t data)
{
    printf("pcf5063x_write: writing addr %02x: %02x\n", addr, data);

    switch (addr) {
    default:
        printf("pcf5063x_write: unknown addr %02x\n", addr);
        s->regs[addr] = data;
        break;
    }
}

static int pcf5063x_event(I2CSlave *slave, enum i2c_event event)
{
    Pcf5063xState *s = PCF5063X(slave);

    s->has_word = false;

    return 0;
}

static uint8_t pcf5063x_recv(I2CSlave *slave)
{
    Pcf5063xState *s = PCF5063X(slave);
    uint8_t r = 0;

    r = pcf5063x_read(s, s->word);
    s->word++;
    
    return r;
}

static int pcf5063x_send(I2CSlave *slave, uint8_t data)
{
    Pcf5063xState *s = PCF5063X(slave);
    
    if (!s->has_word) {
        printf("pcf5063x_send: setting word %02x\n", data);
        s->has_word = true;
        s->word = data;
    } else {
        printf("pcf5063x_send: writing word %02x: %02x\n", s->word, data);
        pcf5063x_write(s, s->word, data);
        s->word++;
    }

    return 0;
}

static void pcf5063x_reset(DeviceState *dev)
{
    Pcf5063xState *s = PCF5063X(dev);

    /* Reset registers */
    memset(s->regs, 0, sizeof(s->regs));

    s->regs[PCF5063X_OOCSHDWN] = 0x08;
}

static void pcf5063x_class_init(ObjectClass *klass, void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *isc = I2C_SLAVE_CLASS(klass);

    dc->reset = pcf5063x_reset;
    // rc->phases.enter = pcf5063x_reset_enter;
    // dc->vmsd = &vmstate_pcf5063x;
    isc->event = pcf5063x_event;
    isc->recv = pcf5063x_recv;
    isc->send = pcf5063x_send;
}

static const TypeInfo pcf5063x_types[] = {
    {
        .name = TYPE_PCF5063X,
        .parent = TYPE_I2C_SLAVE,
        .instance_size = sizeof(Pcf5063xState),
        .class_init = pcf5063x_class_init,
    },
};
DEFINE_TYPES(pcf5063x_types);

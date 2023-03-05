#include "hw/arm/ipod_nano3g_clock.h"

static void S5L8702_clock1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    IPodNano3GClockState *s = (struct IPodNano3GClockState *) opaque;

    switch (addr) {
        case CLOCK1_CONFIG0:
            s->config0 = val;
            break;
        case CLOCK1_CONFIG1:
            s->config1 = val;
            break;
        case CLOCK1_CONFIG2:
            s->config2 = val;
            break;
        case CLOCK1_PLLLOCK:
            s->plllock = val;
            break;
        case CLOCK1_PLLMODE:
            //s->pllmode = val;
            break;
      default:
            break;
    }
}

static uint64_t S5L8702_clock1_read(void *opaque, hwaddr addr, unsigned size)
{
    IPodNano3GClockState *s = (struct IPodNano3GClockState *) opaque;

    switch (addr) {
        case CLOCK1_CONFIG0:
            return s->config0;
        case CLOCK1_CONFIG1:
            return s->config1;
        case CLOCK1_CONFIG2:
            return s->config2;
        case CLOCK1_PLL0CON:
            return s->pll0con;
        case CLOCK1_PLL1CON:
            return s->pll1con;
        case CLOCK1_PLL2CON:
            return s->pll2con;
        case CLOCK1_PLL3CON:
            return s->pll3con;
        case CLOCK1_PLLLOCK:
            return s->plllock;
        case CLOCK1_PLLMODE:
            return s->pllmode;
      default:
            break;
    }
    return 0;
}

static const MemoryRegionOps clock1_ops = {
    .read = S5L8702_clock1_read,
    .write = S5L8702_clock1_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void S5L8702_clock_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(sbd);
    IPodNano3GClockState *s = IPOD_NANO3G_CLOCK(dev);

    s->plllock = 1 | 2 | 4 | 8;

    // iPod Nano5G in-bootrom dump (wIndex):
    // 3c500000: 00100040 40424000 00800080 00800080  ...@@B@.........
    // 3c500010: 00800000 00800000 00000000 00000000  ................
    // 3c500020: 016c0006 01480002 01480002 00000000  .l...H...H......
    // 3c500030: 201c0000 00000000 00000000 00220000   ............"..
    // 3c500040: 11000000 01000100 e31f1f00 f7afabff  ................
    // 3c500050: 00000000 01000000 f5010000 00000000  ................
    // 3c500060: 0a000000 00000000 ffd70300 ffffff06  ................
    // 3c500070: 00000000 00000000 00000000 00000000  ................


    // iPod Nano5G post-WTF dump (u-boot):
    // => md.l 3c500000
    // 3c500000: 40009000 00404040 80001014 c000c000  ...@@@@.........
    // 3c500010: 0000c000 80008000 00000000 00000000  ................
    // 3c500020: 0900ce01 02004801 02004801 00000000  .....H...H......
    // 3c500030: 00028488 00000000 00000000 00000000  ................
    // 3c500040: 00000011 00010001 001fa5c1 f42ae925  ............%.*.

    uint64_t config0 = 0x0;
    config0 = 0x40001000;
    s->config0 = config0;

    uint64_t config1 = 0x0;
    config1 = 0x00404240;
    s->config1 = config1;

    uint64_t config2 = 0x0;
    config2 = 0x80008000;
    s->config2 = config2;

    s->pll0con = 0x06006c01;
    s->pll1con = 0x02004801;
    s->pll2con = 0x02004801;
    s->pll3con = 0x00000000;

    s->pllmode = 0x00010001;

    memory_region_init_io(&s->iomem, obj, &clock1_ops, s, "clock", 0x1000);
}

static void S5L8702_clock_class_init(ObjectClass *klass, void *data)
{

}

static const TypeInfo ipod_nano3g_clock_info = {
    .name          = TYPE_IPOD_NANO3G_CLOCK,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodNano3GClockState),
    .instance_init = S5L8702_clock_init,
    .class_init    = S5L8702_clock_class_init,
};

static void ipod_nano3g_machine_types(void)
{
    type_register_static(&ipod_nano3g_clock_info);
}

type_init(ipod_nano3g_machine_types)
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "sysemu/runstate.h"
#include "hw/misc/s5l8702-clk.h"
#include "trace.h"

/* CLK register offsets */
enum {
    REG_CLKCON0 = 0x0000,
    REG_CLKCON1 = 0x0004,
    REG_CLKCON2 = 0x0008,
    REG_CLKCON3 = 0x000C,
    REG_CLKCON4 = 0x0010,
    REG_CLKCON5 = 0x0014,
    // REG_UNK1 = 0x0018,
    // REG_UNK2 = 0x001C,
    REG_PLL0PMS = 0x0020,
    REG_PLL1PMS = 0x0024,
    REG_PLL2PMS = 0x0028,
    // REG_UNK3 = 0x002C,
    REG_PLL0LCNT = 0x0030,
    REG_PLL1LCNT = 0x0034,
    REG_PLL2LCNT = 0x0038,
    // REG_UNK4 = 0x003C,
    REG_PLLLOCK = 0x0040,
    REG_PLLMODE = 0x0044,
    REG_PWRCON0 = 0x0048,
    REG_PWRCON1 = 0x004C,
    REG_SWRCON = 0x0050,
    REG_RSTSR = 0x0054,
    REG_PWRCON2 = 0x0058,
    // REG_UNK5 = 0x005C,
    // REG_UNK6 = 0x0060,
    // REG_UNK7 = 0x0064,
    REG_PWRCON3 = 0x0068,
    REG_PWRCON4 = 0x006C,
};

// magic value written to REG_SWRCON to reset the SoC
#define SWRCON_RESET_MAGIC 0xaa5

#define REG_INDEX(offset) (offset / sizeof(uint32_t))

/* CLK register reset values */
enum {
    REG_CLKCON0_RST = 0x00000000,
    REG_CLKCON1_RST = 0x00000000,
    REG_CLKCON2_RST = 0x00000000,
    REG_CLKCON3_RST = 0x00000000,
    REG_CLKCON4_RST = 0x00000000,
    REG_CLKCON5_RST = 0x00000000,
    // REG_UNK1_RST = 0x00000000,
    // REG_UNK2_RST = 0x00000000,
    REG_PLL0PMS_RST = 0x00000000,
    REG_PLL1PMS_RST = 0x00000000,
    REG_PLL2PMS_RST = 0x00000000,
    // REG_UNK3_RST = 0x00000000,
    REG_PLL0LCNT_RST = 0x00000000,
    REG_PLL1LCNT_RST = 0x00000000,
    REG_PLL2LCNT_RST = 0x00000000,
    // REG_UNK4_RST = 0x00000000,
    REG_PLLLOCK_RST = 0xFFFFFFFF, // bootrom waits for (PLLLOCK & 0x11)
    REG_PLLMODE_RST = 0x00000000,
    REG_PWRCON0_RST = 0x00000000,
    REG_PWRCON1_RST = 0x00000000,
    REG_SWRCON_RST = 0x00000000,
    REG_RSTSR_RST = 0x00000000,
    REG_PWRCON2_RST = 0x00000000,
    // REG_UNK5_RST = 0x00000000,
    // REG_UNK6_RST = 0x00000000,
    // REG_UNK7_RST = 0x00000000,
    REG_PWRCON3_RST = 0x00000000,
    REG_PWRCON4_RST = 0x00000000,
};

static uint64_t s5l8702_clk_read(void *opaque, hwaddr offset, unsigned size) {
    const S5L8702ClkState *s = S5L8702_CLK(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch (offset) {
    default:
        // qemu_log_mask(LOG_UNIMP, "%s: unimplemented read (offset 0x%04x)\n", __func__, (uint32_t) offset);
    }

    return s->regs[idx];
}

static void s5l8702_clk_write(void *opaque, hwaddr offset, uint64_t val, unsigned size) {
    S5L8702ClkState *s = S5L8702_CLK(opaque);
    const uint32_t idx = REG_INDEX(offset);

    switch (offset) {
    case REG_SWRCON:
        if (val == SWRCON_RESET_MAGIC) {
            trace_s5l8702_clk_sw_reset((uint32_t) val);
            s->regs[idx] = (uint32_t) val;
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            return;
        }
        break;
    default:
        // qemu_log_mask(LOG_UNIMP, "%s: unimplemented write (offset 0x%04x, value 0x%08x)\n", __func__, (uint32_t) offset, (uint32_t) val);
        break;
    }

    s->regs[idx] = (uint32_t) val;
}

static const MemoryRegionOps s5l8702_clk_ops = {
    .read = s5l8702_clk_read,
    .write = s5l8702_clk_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    // .valid = {
    //     .min_access_size = 4,
    //     .max_access_size = 4,
    // },
    // .impl.min_access_size = 4,
};

static void s5l8702_clk_reset(DeviceState *dev)
{
    S5L8702ClkState *s = S5L8702_CLK(dev);

    trace_s5l8702_clk_reset();

    /* Reset registers */
    memset(s->regs, 0, sizeof(s->regs));

    /* Set default values for registers */
    s->regs[REG_INDEX(REG_CLKCON0)] = REG_CLKCON0_RST;
    s->regs[REG_INDEX(REG_CLKCON1)] = REG_CLKCON1_RST;
    s->regs[REG_INDEX(REG_CLKCON2)] = REG_CLKCON2_RST;
    s->regs[REG_INDEX(REG_CLKCON3)] = REG_CLKCON3_RST;
    s->regs[REG_INDEX(REG_CLKCON4)] = REG_CLKCON4_RST;
    s->regs[REG_INDEX(REG_CLKCON5)] = REG_CLKCON5_RST;
    // s->regs[REG_INDEX(REG_UNK1)] = REG_UNK1_RST;
    // s->regs[REG_INDEX(REG_UNK2)] = REG_UNK2_RST;
    s->regs[REG_INDEX(REG_PLL0PMS)] = REG_PLL0PMS_RST;
    s->regs[REG_INDEX(REG_PLL1PMS)] = REG_PLL1PMS_RST;
    s->regs[REG_INDEX(REG_PLL2PMS)] = REG_PLL2PMS_RST;
    // s->regs[REG_INDEX(REG_UNK3)] = REG_UNK3_RST;
    s->regs[REG_INDEX(REG_PLL0LCNT)] = REG_PLL0LCNT_RST;
    s->regs[REG_INDEX(REG_PLL1LCNT)] = REG_PLL1LCNT_RST;
    s->regs[REG_INDEX(REG_PLL2LCNT)] = REG_PLL2LCNT_RST;
    // s->regs[REG_INDEX(REG_UNK4)] = REG_UNK4_RST;
    s->regs[REG_INDEX(REG_PLLLOCK)] = REG_PLLLOCK_RST;
    s->regs[REG_INDEX(REG_PLLMODE)] = REG_PLLMODE_RST;
    s->regs[REG_INDEX(REG_PWRCON0)] = REG_PWRCON0_RST;
    s->regs[REG_INDEX(REG_PWRCON1)] = REG_PWRCON1_RST;
    s->regs[REG_INDEX(REG_SWRCON)] = REG_SWRCON_RST;
    s->regs[REG_INDEX(REG_RSTSR)] = REG_RSTSR_RST;
    s->regs[REG_INDEX(REG_PWRCON2)] = REG_PWRCON2_RST;
    // s->regs[REG_INDEX(REG_UNK5)] = REG_UNK5_RST;
    // s->regs[REG_INDEX(REG_UNK6)] = REG_UNK6_RST;
    // s->regs[REG_INDEX(REG_UNK7)] = REG_UNK7_RST;
    s->regs[REG_INDEX(REG_PWRCON3)] = REG_PWRCON3_RST;
    s->regs[REG_INDEX(REG_PWRCON4)] = REG_PWRCON4_RST;
}

static void s5l8702_clk_init(Object *obj)
{
    S5L8702ClkState *s = S5L8702_CLK(obj);

    trace_s5l8702_clk_init();

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_clk_ops, s, TYPE_S5L8702_CLK, S5L8702_CLK_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void s5l8702_clk_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = s5l8702_clk_reset;
}

static const TypeInfo s5l8702_clk_types[] = {
    {
        .name = TYPE_S5L8702_CLK,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_init = s5l8702_clk_init,
        .instance_size = sizeof(S5L8702ClkState),
        .class_init = s5l8702_clk_class_init,
    },
};
DEFINE_TYPES(s5l8702_clk_types);

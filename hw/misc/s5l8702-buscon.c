/*
 * S5L8702 bus controller - the part of it that decides what the CPU sees at
 * address 0.
 *
 * At reset address 0 aliases the BootROM, which is how the core starts
 * executing it. Firmware that wants its own exception vectors builds them at
 * the bottom of IRAM0 (0x22000000) and then flips this register, after which
 * address 0 is IRAM0 and every exception goes to the firmware's table instead
 * of the BootROM's.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "exec/address-spaces.h"
#include "hw/misc/s5l8702-buscon.h"
#include "trace.h"

#define S5L8702_BUSCON_REMAP        0x0C
#define S5L8702_BUSCON_REMAP_ENABLE BIT(0)
#define S5L8702_BUSCON_REMAP_SRAM   BIT(1)

void s5l8702_buscon_select(S5L8702BusConState *s, bool sram) {
    if (!s || !s->brom_alias || !s->sram_alias) return;
    
    trace_s5l8702_buscon_remap(s->remap, sram ? "iram0" : "bootrom");

    // set_enabled is a no-op when the flag already matches
    memory_region_transaction_begin();
    memory_region_set_enabled(s->brom_alias, !sram);
    memory_region_set_enabled(s->sram_alias, sram);
    memory_region_transaction_commit();
}

static void s5l8702_buscon_apply_remap(S5L8702BusConState *s) {
    bool to_sram = (s->remap & S5L8702_BUSCON_REMAP_ENABLE) && (s->remap & S5L8702_BUSCON_REMAP_SRAM);

    if ((s->remap & S5L8702_BUSCON_REMAP_ENABLE) && !to_sram) {
        qemu_log_mask(LOG_UNIMP, "s5l8702-buscon: remap enabled with an unmodelled source (0x%08x), keeping the BootROM at address 0\n", s->remap);
    }

    s5l8702_buscon_select(s, to_sram);
}

static uint64_t s5l8702_buscon_read(void *opaque, hwaddr offset, unsigned size) {
    S5L8702BusConState *s = S5L8702_BUSCON(opaque);
    uint32_t val = 0;

    switch (offset) {
    case S5L8702_BUSCON_REMAP:
        val = s->remap;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read at 0x%04x\n", __func__, (uint32_t)offset);
        break;
    }

    trace_s5l8702_buscon_read((uint32_t)offset, val);
    return val;
}

static void s5l8702_buscon_write(void *opaque, hwaddr offset, uint64_t value, unsigned size) {
    S5L8702BusConState *s = S5L8702_BUSCON(opaque);

    trace_s5l8702_buscon_write((uint32_t)offset, (uint32_t)value);

    switch (offset) {
    case S5L8702_BUSCON_REMAP:
        s->remap = (uint32_t)value;
        s5l8702_buscon_apply_remap(s);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write at 0x%04x = 0x%08x\n", __func__, (uint32_t)offset, (uint32_t)value);
        break;
    }
}

static const MemoryRegionOps s5l8702_buscon_ops = {
    .read = s5l8702_buscon_read,
    .write = s5l8702_buscon_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_buscon_reset(DeviceState *dev) {
    S5L8702BusConState *s = S5L8702_BUSCON(dev);

    s->remap = 0;
    s5l8702_buscon_select(s, false);
}

static void s5l8702_buscon_init(Object *obj) {
    S5L8702BusConState *s = S5L8702_BUSCON(obj);

    memory_region_init_io(&s->iomem, obj, &s5l8702_buscon_ops, s, TYPE_S5L8702_BUSCON, S5L8702_BUSCON_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void s5l8702_buscon_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = s5l8702_buscon_reset;
}

static const TypeInfo s5l8702_buscon_types[] = {
    {
        .name          = TYPE_S5L8702_BUSCON,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(S5L8702BusConState),
        .instance_init = s5l8702_buscon_init,
        .class_init    = s5l8702_buscon_class_init,
    },
};
DEFINE_TYPES(s5l8702_buscon_types);

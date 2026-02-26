/*
 * S5L8702 NAND ECC Engine
 *
 * Ported from the qemu-ipod-nano project.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/misc/s5l8702-nand-ecc.h"
#include "trace.h"

static uint64_t s5l8702_nand_ecc_read(void *opaque, hwaddr addr, unsigned size) {
    switch (addr) {
    case NANDECC_STATUS:
        /* All ECC operations succeed immediately in emulation */
        return 0;
    default:
        break;
    }
    return 0;
}

static void s5l8702_nand_ecc_write(void *opaque, hwaddr addr, uint64_t val, unsigned size) {
    S5L8702NandEccState *s = S5L8702_NAND_ECC(opaque);

    switch (addr) {
    case NANDECC_START:
        trace_s5l8702_nand_ecc_start();
        qemu_irq_raise(s->irq);
        break;
    case NANDECC_CLEARINT:
        trace_s5l8702_nand_ecc_clearint();
        qemu_irq_lower(s->irq);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps s5l8702_nand_ecc_ops = {
    .read       = s5l8702_nand_ecc_read,
    .write      = s5l8702_nand_ecc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_nand_ecc_init(Object *obj) {
    trace_s5l8702_nand_ecc_init();
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    S5L8702NandEccState *s = S5L8702_NAND_ECC(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_nand_ecc_ops, s, "s5l8702-nand-ecc", S5L8702_NAND_ECC_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static void s5l8702_nand_ecc_reset(DeviceState *dev) {
    trace_s5l8702_nand_ecc_reset();
    S5L8702NandEccState *s = S5L8702_NAND_ECC(dev);

    s->data_addr = 0;
    s->ecc_addr  = 0;
    s->status    = 0;
    s->setup     = 0;
}

static void s5l8702_nand_ecc_class_init(ObjectClass *oc, void *data) {
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->reset = s5l8702_nand_ecc_reset;
}

static const TypeInfo s5l8702_nand_ecc_info = {
    .name          = TYPE_S5L8702_NAND_ECC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(S5L8702NandEccState),
    .instance_init = s5l8702_nand_ecc_init,
    .class_init    = s5l8702_nand_ecc_class_init,
};

static void s5l8702_nand_ecc_register_types(void) {
    type_register_static(&s5l8702_nand_ecc_info);
}

type_init(s5l8702_nand_ecc_register_types)

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"

#define TYPE_VIC_EDGE_GLUE "vic-edge-glue"
#define VIC_EDGE_GLUE(obj) \
    OBJECT_CHECK(VICEdgeGlueState, (obj), TYPE_VIC_EDGE_GLUE)

typedef struct VICEdgeGlueState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    /* 64 Outputs: 0-31 go to VIC0, 32-63 go to VIC1 */
    qemu_irq outputs[64];

    /* State */
    uint32_t vic0_edge_mode; // VIC0EDGE0
    uint32_t vic1_edge_mode; // VIC1EDGE0
    
    /* The Latches (Hold the interrupt high until cleared) */
    uint32_t vic0_latch;
    uint32_t vic1_latch;

    /* Track previous input levels to detect real rising edges */
    uint64_t prev_levels;

} VICEdgeGlueState;

static void vic_edge_update(VICEdgeGlueState *s) {
    int i;
    
    /* Update VIC0 Lines */
    for (i = 0; i < 32; i++) {
        /* If Edge Mode is ON, use the Latch. If OFF, we rely on the 
           passthrough logic handled in set_irq, but strictly speaking,
           the output should be (InputLevel OR Latch) depending on impl.
           Here we simplify: The set_irq logic drives the output. */
    }
}

static uint64_t glue_read(void *opaque, hwaddr offset, unsigned size) {
    VICEdgeGlueState *s = (VICEdgeGlueState *)opaque;
    
    switch (offset) {
    case 0x00: return s->vic0_edge_mode; // VIC0EDGE0
    case 0x04: return s->vic1_edge_mode; // VIC1EDGE0
    case 0x08: return s->vic0_latch;     // VIC0EDGE1: read pending edge latch bits
    case 0x0C: return s->vic1_latch;     // VIC1EDGE1: read pending edge latch bits
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "glue_read: Bad offset %x\n", (int)offset);
        return 0;
    }
}

static void glue_write(void *opaque, hwaddr offset, uint64_t val, unsigned size) {
    VICEdgeGlueState *s = (VICEdgeGlueState *)opaque;
    int i;

    switch (offset) {
    case 0x00: /* VIC0EDGE0: Enable/Disable Edge Mode */
        s->vic0_edge_mode = val;
        /* If switching back to level mode, we might want to auto-clear latches,
           but usually hardware leaves them alone. */
        break;
        
    case 0x04: /* VIC1EDGE0 */
        s->vic1_edge_mode = val;
        break;

    case 0x08: /* VIC0EDGE1: Clear Latch */
        /* Assuming writing 1 clears the interrupt */
        s->vic0_latch &= ~val;
        
        /* Update outputs for VIC0 */
        for (i = 0; i < 32; i++) {
            if ((s->vic0_edge_mode & (1U << i)) && (val & (1U << i))) {
                qemu_set_irq(s->outputs[i], 0);
            }
        }
        break;

    case 0x0C: /* VIC1EDGE1: Clear Latch */
        s->vic1_latch &= ~val;
        
        /* Update outputs for VIC1 */
        for (i = 0; i < 32; i++) {
            if ((s->vic1_edge_mode & (1U << i)) && (val & (1U << i))) {
                qemu_set_irq(s->outputs[i + 32], 0);
            }
        }
        break;
        
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "glue_write: Bad offset %x\n", (int)offset);
    }
}

static const MemoryRegionOps glue_ops = {
    .read = glue_read,
    .write = glue_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/* 
 * This is the Input Handler.
 * Peripherals connect HERE, not to the VIC directly.
 */
static void glue_set_irq(void *opaque, int irq, int level) {
    VICEdgeGlueState *s = (VICEdgeGlueState *)opaque;
    int vic_index = (irq < 32) ? 0 : 1;
    int bit_index = irq % 32;
    uint32_t *mode_reg = (vic_index == 0) ? &s->vic0_edge_mode : &s->vic1_edge_mode;
    uint32_t *latch_reg = (vic_index == 0) ? &s->vic0_latch : &s->vic1_latch;
    int output_index = irq;

    int is_edge_mode = (*mode_reg >> bit_index) & 1;
    int prev_level = (s->prev_levels >> irq) & 1;

    /* Update stored level */
    if (level) {
        s->prev_levels |= (1ULL << irq);
    } else {
        s->prev_levels &= ~(1ULL << irq);
    }

    if (is_edge_mode) {
        /* Only latch and assert on a true 0->1 rising edge */
        if (level == 1 && prev_level == 0) {
            *latch_reg |= (1U << bit_index);
            qemu_set_irq(s->outputs[output_index], 1);
        }
        /* Do NOT lower the output here; cleared only via register write */
    } else {
        /* Level mode: passthrough */
        qemu_set_irq(s->outputs[output_index], level);
    }
}

static void vic_edge_glue_init(Object *obj) {
    VICEdgeGlueState *s = VIC_EDGE_GLUE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &glue_ops, s, "vic-edge-glue", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);

    /* Inputs from peripherals (0-63) */
    qdev_init_gpio_in(DEVICE(obj), glue_set_irq, 64);

    /* Outputs to VICs (0-63) */
    qdev_init_gpio_out(DEVICE(obj), s->outputs, 64);
}

static void vic_edge_glue_reset(DeviceState *d) {
    VICEdgeGlueState *s = VIC_EDGE_GLUE(d);
    s->vic0_edge_mode = 0;
    s->vic1_edge_mode = 0;
    s->vic0_latch = 0;
    s->vic1_latch = 0;
    s->prev_levels = 0;
}

static void vic_edge_glue_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = vic_edge_glue_reset;
}

static const TypeInfo vic_edge_glue_info = {
    .name          = TYPE_VIC_EDGE_GLUE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(VICEdgeGlueState),
    .instance_init = vic_edge_glue_init,
    .class_init    = vic_edge_glue_class_init,
};

static void vic_edge_glue_register_types(void) {
    type_register_static(&vic_edge_glue_info);
}

type_init(vic_edge_glue_register_types)

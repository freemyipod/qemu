#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/i2c/s5l8702-i2c.h"
#include "trace.h"

#define S5L8702_I2C_IICCON      0x00
#define S5L8702_I2C_IICSTAT     0x04
#define S5L8702_I2C_IICADD      0x08
#define S5L8702_I2C_IICDS       0x0C
#define S5L8702_I2C_IIUNK10     0x10
#define S5L8702_I2C_IIUNK14     0x14
#define S5L8702_I2C_IIUNK18     0x18
#define S5L8702_I2C_IICSTAT2    0x20

/* IICCON */
#define S5L8702_I2C_IICCON_ACK_GEN      BIT(7)
#define S5L8702_I2C_IICCON_CKSEL        BIT(6)
#define S5L8702_I2C_IICCON_INT_EN       BIT(5)
#define S5L8702_I2C_IICCON_IRQ          BIT(4)
#define S5L8702_I2C_IICCON_CK_REG(x)    ((x) & 0x7)
#define S5L8702_I2C_IICCON_CK_REG_MASK  0x7

/* IICSTAT */
#define S5L8702_I2C_IICSTAT_MODE_SEL(x)     (((x) & 0x3) << 6)
#define S5L8702_I2C_IICSTAT_MODE_SEL_MASK   (0x3 << 6)
#define S5L8702_I2C_IICSTAT_MODE_BB         BIT(5)
#define S5L8702_I2C_IICSTAT_MODE_SOE        BIT(4)
#define S5L8702_I2C_IICSTAT_MODE_LBA        BIT(3)
#define S5L8702_I2C_IICSTAT_MODE_AAS        BIT(2)
#define S5L8702_I2C_IICSTAT_MODE_ADDR_ZERO  BIT(1)
#define S5L8702_I2C_IICSTAT_MODE_LRB        BIT(0)

/* IICADD */
#define S5L8702_I2C_IICADD_S_ADDR(x)    (((x) & 0x7F) << 1)
#define S5L8702_I2C_IICADD_S_ADDR_MASK  (0x7F << 1)

/* IICDS */
#define S5L8702_I2C_IICDS_DATA(x)   (((x) & 0xFF) << 0)

static void s5l8702_i2c_update_irq(S5L8702I2cState *s) {
    /* Assert the physical IRQ line if the pending bit is 1 */
    bool irq_pend = (s->iiccon & S5L8702_I2C_IICCON_IRQ) != 0;
    qemu_set_irq(s->irq, irq_pend ? 1 : 0);
}

static void s5l8702_i2c_resume_transfer(S5L8702I2cState *s)
{
    uint32_t mode = s->iicstat & 0xF0;

    if (mode == 0xF0) { // Resume TX
        int ack = i2c_send(s->bus, (uint8_t) s->iicds);
        if (ack) {
            s->iicstat |= S5L8702_I2C_IICSTAT_MODE_LRB; // NACK from slave
        } else {
            s->iicstat &= ~S5L8702_I2C_IICSTAT_MODE_LRB; // ACK from slave
        }
        s->iiccon |= S5L8702_I2C_IICCON_IRQ; 
        s->iicstat2 |= BIT(8);
    }
    else if (mode == 0xB0) { // Resume RX
        s->iicds = s->rx_shift_register;
        s->rx_shift_register = i2c_recv(s->bus);
        
        /* 
         * If the guest enabled ACK_GEN (Bit 7), we respond with ACK (LRB = 0).
         * If the guest disabled it, we respond with NACK (LRB = 1).
         */
        if (s->iiccon & S5L8702_I2C_IICCON_ACK_GEN) {
            s->iicstat &= ~S5L8702_I2C_IICSTAT_MODE_LRB; // ACK
        } else {
            s->iicstat |= S5L8702_I2C_IICSTAT_MODE_LRB;  // NACK
        }

        s->iiccon |= S5L8702_I2C_IICCON_IRQ; 
        s->iicstat2 |= BIT(8); 
    }
}

static uint64_t s5l8702_i2c_read(void *opaque, hwaddr offset, unsigned size) {
    const S5L8702I2cState *s = S5L8702_I2C(opaque);
    uint32_t r = 0;

    switch (offset) {
    case S5L8702_I2C_IICCON:
        r = s->iiccon;
        trace_s5l8702_i2c_read("IICCON", r);
        break;
    case S5L8702_I2C_IICSTAT:
        r = s->iicstat;
        trace_s5l8702_i2c_read("IICSTAT", r);
        break;
    case S5L8702_I2C_IICADD:
        r = s->iicadd;
        trace_s5l8702_i2c_read("IICADD", r);
        break;
    case S5L8702_I2C_IICDS:
        r = s->iicds;
        trace_s5l8702_i2c_read("IICDS", r);
        break;
    case S5L8702_I2C_IIUNK10:
        r = s->iicunk10;
        trace_s5l8702_i2c_read("IIUNK10", r);
        break;
    case S5L8702_I2C_IIUNK14:
        r = s->iicunk14;
        trace_s5l8702_i2c_read("IIUNK14", r);
        break;
    case S5L8702_I2C_IIUNK18:
        r = s->iicunk18;
        trace_s5l8702_i2c_read("IIUNK18", r);
        break;
    case S5L8702_I2C_IICSTAT2:
        r = s->iicstat2;
        trace_s5l8702_i2c_read("IICSTAT2", r);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read (offset 0x%04x)\n",
                      __func__, (uint32_t) offset);
    }

    return r;
}

static void s5l8702_i2c_write(void *opaque, hwaddr offset, uint64_t val, unsigned size) {
    S5L8702I2cState *s = S5L8702_I2C(opaque);

    switch (offset) {
    case S5L8702_I2C_IICCON:
    {
        trace_s5l8702_i2c_write("IICCON", (uint32_t) val);
        
        bool irq_was_pending = (s->iiccon & S5L8702_I2C_IICCON_IRQ) != 0;
        bool irq_cleared_by_guest = irq_was_pending && ((val & S5L8702_I2C_IICCON_IRQ) == 0);

        s->iiccon = ((uint32_t)val & ~S5L8702_I2C_IICCON_IRQ) | 
                    (irq_cleared_by_guest ? 0 : (s->iiccon & S5L8702_I2C_IICCON_IRQ));

        if (irq_cleared_by_guest) {
            s->iicstat2 &= ~BIT(8); // Keep Apple flag in sync
            s5l8702_i2c_resume_transfer(s);
        }
        s5l8702_i2c_update_irq(s);
        break;
    }
    case S5L8702_I2C_IICSTAT:
    {
        trace_s5l8702_i2c_write("IICSTAT", (uint32_t) val);

        uint32_t mode = val & 0xF0;
        s->iicstat = (s->iicstat & ~0xF0) | mode;

        if (!(val & 0x10)) {
            // Tx/Rx Disabled. Abort transfer and clear busy bit.
            if (s->iicstat & S5L8702_I2C_IICSTAT_MODE_BB) {
                i2c_end_transfer(s->bus);
                s->iicstat &= ~S5L8702_I2C_IICSTAT_MODE_BB; // Bus is free
            }
        } else {
            if (mode == 0xF0) { // START TX
                int ack = i2c_start_send(s->bus, ((uint8_t) s->iicds) >> 1);
                if (ack) {
                    s->iicstat |= S5L8702_I2C_IICSTAT_MODE_LRB; // NACK
                } else {
                    s->iicstat &= ~S5L8702_I2C_IICSTAT_MODE_LRB; // ACK
                }
                s->iicstat |= S5L8702_I2C_IICSTAT_MODE_BB; // Bus is busy
                s->iiccon |= S5L8702_I2C_IICCON_IRQ; 
                s->iicstat2 |= BIT(8);
            }
            else if (mode == 0xB0) { // START RX
                int ack = i2c_start_recv(s->bus, ((uint8_t) s->iicds) >> 1);
                if (ack) {
                    s->iicstat |= S5L8702_I2C_IICSTAT_MODE_LRB; // NACK
                } else {
                    s->iicstat &= ~S5L8702_I2C_IICSTAT_MODE_LRB; // ACK
                }
                s->iicstat |= S5L8702_I2C_IICSTAT_MODE_BB; // Bus is busy
                s->iiccon |= S5L8702_I2C_IICCON_IRQ; 
                s->iicstat2 |= BIT(8);
            }
            else if (mode == 0xD0) { // STOP TX
                i2c_end_transfer(s->bus);
                s->iicstat &= ~S5L8702_I2C_IICSTAT_MODE_BB; // Bus is free
            }
            else if (mode == 0x90) { // STOP RX
                i2c_end_transfer(s->bus);
                s->iicstat &= ~S5L8702_I2C_IICSTAT_MODE_BB; // Bus is free
            }
        }

        s5l8702_i2c_update_irq(s);
        break;
    }
    case S5L8702_I2C_IICADD:
        trace_s5l8702_i2c_write("IICADD", (uint32_t) val);
        s->iicadd = (uint32_t) val;
        break;
    case S5L8702_I2C_IICDS:
        trace_s5l8702_i2c_write("IICDS", (uint32_t) val);
        s->iicds = (uint32_t) val;
        break;
    case S5L8702_I2C_IIUNK10:
        trace_s5l8702_i2c_write("IIUNK10", (uint32_t) val);
        s->iicunk10 = (uint32_t) val;
        break;
    case S5L8702_I2C_IIUNK14:
        trace_s5l8702_i2c_write("IIUNK14", (uint32_t) val);
        s->iicunk14 = (uint32_t) val;
        break;
    case S5L8702_I2C_IIUNK18:
        trace_s5l8702_i2c_write("IIUNK18", (uint32_t) val);
        s->iicunk18 = (uint32_t) val;
        break;
    case S5L8702_I2C_IICSTAT2:
    {
        trace_s5l8702_i2c_write("IICSTAT2", (uint32_t) val);
        bool irq_was_pending = (s->iicstat2 & BIT(8)) != 0;
        
        s->iicstat2 &= ~(uint32_t) val; 

        // If the guest clears bit 8, sync legacy registers and RESUME TRANSFER!
        if (irq_was_pending && ((s->iicstat2 & BIT(8)) == 0)) {
            s->iiccon &= ~S5L8702_I2C_IICCON_IRQ; 
            s5l8702_i2c_resume_transfer(s);
        }
        
        s5l8702_i2c_update_irq(s);
        break;
    }
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write (offset 0x%04x, value 0x%08x)\n",
                      __func__, (uint32_t) offset, (uint32_t) val);
    }
}

static const MemoryRegionOps s5l8702_i2c_ops = {
    .read = s5l8702_i2c_read,
    .write = s5l8702_i2c_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_i2c_reset(DeviceState *dev) {
    S5L8702I2cState *s = S5L8702_I2C(dev);

    /* Reset registers */
    s->iiccon = 0;
    s->iicstat = 0;
    s->iicadd = 0;
    s->iicds = 0;
    s->iicunk10 = 0;
    s->iicunk14 = 0;
    s->iicunk18 = 0;
    s->iicstat2 = 0;
    
    s5l8702_i2c_update_irq(s);
}

static void s5l8702_i2c_init(Object *obj) {
    S5L8702I2cState *s = S5L8702_I2C(obj);

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_i2c_ops, s, TYPE_S5L8702_I2C, S5L8702_I2C_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    s->bus = i2c_init_bus(DEVICE(obj), "s5l8702-i2c");

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void s5l8702_i2c_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = s5l8702_i2c_reset;
}

static const TypeInfo s5l8702_i2c_types[] = {
    {
        .name = TYPE_S5L8702_I2C,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_init = s5l8702_i2c_init,
        .instance_size = sizeof(S5L8702I2cState),
        .class_init = s5l8702_i2c_class_init,
    },
};
DEFINE_TYPES(s5l8702_i2c_types);
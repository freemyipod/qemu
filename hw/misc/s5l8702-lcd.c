#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/s5l8702-lcd.h"

#define LCD_CONFIG  0x00
#define LCD_WCMD    0x04
#define LCD_STATUS  0x1c
#define LCD_PHTIME  0x20
#define LCD_WDATA   0x40

#define LCD_STATUS_READY    BIT(1)

static uint64_t s5l8702_lcd_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    const S5L8702LcdState *s = S5L8702_LCD(opaque);
    uint32_t r = 0;

    switch (offset) {
    case LCD_CONFIG:
        r = s->config;
        printf("s5l8702_lcd_read: LCD_CONFIG = 0x%08x\n", r);
        break;
    case LCD_WCMD:
        r = s->wcmd;
        printf("s5l8702_lcd_read: LCD_WCMD = 0x%08x\n", r);
        break;
    case LCD_STATUS:
        r = s->status;
        r = 0xFFFFFFEF; // TODO: Fix me!
        printf("s5l8702_lcd_read: LCD_STATUS = 0x%08x\n", r);
        break;
    case LCD_PHTIME:
        r = s->phtime;
        printf("s5l8702_lcd_read: LCD_PHTIME = 0x%08x\n", r);
        break;
    case LCD_WDATA:
        r = s->wdata;
        printf("s5l8702_lcd_read: LCD_WDATA = 0x%08x\n", r);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read (offset 0x%04x)\n",
                      __func__, (uint32_t) offset);
    }

    return r;
}

static void s5l8702_lcd_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    S5L8702LcdState *s = S5L8702_LCD(opaque);

    switch (offset) {
    case LCD_CONFIG:
        s->config = (uint32_t) val;
        printf("s5l8702_lcd_write: LCD_CONFIG = 0x%08x\n", (uint32_t) val);
        break;
    case LCD_WCMD:
        s->wcmd = (uint32_t) val;
        printf("s5l8702_lcd_write: LCD_WCMD = 0x%08x\n", (uint32_t) val);

        switch (s->wcmd) {
            case 0x28: // DISPLAY_OFF
                printf("s5l8702_lcd_write: DISPLAY_OFF\n");
                break;
            case 0x29: // DISPLAY_ON
                printf("s5l8702_lcd_write: DISPLAY_ON\n");
                break;
            default:
                qemu_log_mask(LOG_UNIMP, "%s: unimplemented lcd command (0x%08x)\n",
                              __func__, s->wcmd);
        }
        break;
    case LCD_STATUS:
        s->status = (uint32_t) val;
        printf("s5l8702_lcd_write: LCD_STATUS = 0x%08x\n", (uint32_t) val);
        break;
    case LCD_PHTIME:
        s->phtime = (uint32_t) val;
        printf("s5l8702_lcd_write: LCD_PHTIME = 0x%08x\n", (uint32_t) val);
        break;
    case LCD_WDATA:
        s->wdata = (uint32_t) val;
        printf("s5l8702_lcd_write: LCD_WDATA = 0x%08x\n", (uint32_t) val);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write (offset 0x%04x, value 0x%08x)\n",
                      __func__, (uint32_t) offset, (uint32_t) val);
    }
}

static const MemoryRegionOps s5l8702_lcd_ops = {
    .read = s5l8702_lcd_read,
    .write = s5l8702_lcd_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_lcd_reset(DeviceState *dev)
{
    S5L8702LcdState *s = S5L8702_LCD(dev);

    printf("s5l8702_lcd_reset\n");

    /* Reset registers */
    s->config = 0;
    s->wcmd = 0;
    s->status = LCD_STATUS_READY;
    s->phtime = 0;
    s->wdata = 0;
}

static void fb_invalidate_display(void *opaque)
{
    S5L8702LcdState *s = S5L8702_LCD(opaque);

    s->invalidate = true;
}

static void fb_update_display(void *opaque)
{
    S5L8702LcdState *s = opaque;
    DisplaySurface *surface = qemu_console_surface(s->con);
//    int first = 0;
//    int last = 0;
//    int src_width = 0;
//    int dest_width = 0;
//    uint32_t xoff = 0, yoff = 0;
//
//    if (s->lock || !s->config.xres) {
//        return;
//    }
//
//    src_width = bcm2835_fb_get_pitch(&s->config);
//    if (fb_use_offsets(&s->config)) {
//        xoff = s->config.xoffset;
//        yoff = s->config.yoffset;
//    }
//
//    dest_width = s->config.xres;
//
//    switch (surface_bits_per_pixel(surface)) {
//        case 0:
//            return;
//        case 8:
//            break;
//        case 15:
//            dest_width *= 2;
//            break;
//        case 16:
//            dest_width *= 2;
//            break;
//        case 24:
//            dest_width *= 3;
//            break;
//        case 32:
//            dest_width *= 4;
//            break;
//        default:
//            hw_error("bcm2835_fb: bad color depth\n");
//            break;
//    }
//
//    if (s->invalidate) {
//        hwaddr base = s->config.base + xoff + (hwaddr)yoff * src_width;
//        framebuffer_update_memory_section(&s->fbsection, s->dma_mr,
//                                          base,
//                                          s->config.yres, src_width);
//    }
//
//    framebuffer_update_display(surface, &s->fbsection,
//                               s->config.xres, s->config.yres,
//                               src_width, dest_width, 0, s->invalidate,
//                               draw_line_src16, s, &first, &last);
//
//    if (first >= 0) {
//        dpy_gfx_update(s->con, 0, first, s->config.xres,
//                       last - first + 1);
//    }
//
    s->invalidate = false;
}

static const GraphicHwOps vgafb_ops = {
        .invalidate  = fb_invalidate_display,
        .gfx_update  = fb_update_display,
};

static void s5l8702_lcd_init(Object *obj)
{
    S5L8702LcdState *s = S5L8702_LCD(obj);

    printf("s5l8702_lcd_init\n");

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_lcd_ops, s, TYPE_S5L8702_LCD, S5L8702_LCD_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    s->con = graphic_console_init(DEVICE(obj), 0, &vgafb_ops, s);
//    qemu_console_resize(s->con, s->config.xres, s->config.yres);
    qemu_console_resize(s->con, 320, 240);
}

static void s5l8702_lcd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = s5l8702_lcd_reset;
}

static const TypeInfo s5l8702_lcd_types[] = {
    {
        .name = TYPE_S5L8702_LCD,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_init = s5l8702_lcd_init,
        .instance_size = sizeof(S5L8702LcdState),
        .class_init = s5l8702_lcd_class_init,
    },
};
DEFINE_TYPES(s5l8702_lcd_types);

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/s5l8702-lcd.h"
#include "ui/pixel_ops.h"
#include "ui/console.h"
#include "hw/display/framebuffer.h"

//#define LCD_CONFIG  0x00
//#define LCD_WCMD    0x04
//#define LCD_STATUS  0x1c
//#define LCD_PHTIME  0x20
//#define LCD_WDATA   0x40

#define LCD_CONFIG (0x000)
#define LCD_WCMD   (0x004)
#define LCD_RCMD   (0x00c)
#define LCD_RDATA  (0x010)
#define LCD_DBUFF  (0x014)
#define LCD_INTCON (0x018)
#define LCD_STATUS (0x01c)
#define LCD_PHTIME (0x020)
#define LCD_WDATA  (0x040)

#define LCD_STATUS_READY    BIT(1)

static uint64_t s5l8702_lcd_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    const S5L8702LcdState *s = S5L8702_LCD(opaque);
    uint32_t r = 0;

//    switch (offset) {
//    case LCD_CONFIG:
//        r = s->config;
//        printf("s5l8702_lcd_read: LCD_CONFIG = 0x%08x\n", r);
//        break;
//    case LCD_WCMD:
//        r = s->wcmd;
//        printf("s5l8702_lcd_read: LCD_WCMD = 0x%08x\n", r);
//        break;
//    case LCD_STATUS:
//        r = s->status;
//        r = 0xFFFFFFEF; // TODO: Fix me!
//        printf("s5l8702_lcd_read: LCD_STATUS = 0x%08x\n", r);
//        break;
//    case LCD_PHTIME:
//        r = s->phtime;
//        printf("s5l8702_lcd_read: LCD_PHTIME = 0x%08x\n", r);
//        break;
//    case LCD_WDATA:
//        r = s->wdata;
//        printf("s5l8702_lcd_read: LCD_WDATA = 0x%08x\n", r);
//        break;
//    default:
//        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read (offset 0x%04x)\n",
//                      __func__, (uint32_t) offset);
//    }

    switch(offset) {
        case LCD_CONFIG:
            r = s->lcd_config;
            break;
        case LCD_WCMD:
            r = s->lcd_wcmd;
            break;
        case LCD_RCMD:
            r = s->lcd_rcmd;
            break;
        case LCD_RDATA:
            r = s->lcd_rdata;
            break;
        case LCD_DBUFF:
            r = s->lcd_dbuff;
            break;
        case LCD_INTCON:
            r = s->lcd_intcon;
            break;
        case LCD_STATUS:
            r = 0xFFFFFFEF;
            break;
        case LCD_PHTIME:
            r = s->lcd_phtime;
            break;
        case LCD_WDATA:
            r = s->lcd_wdata;
            break;
        default:
//            printf("%s: read invalid location 0x%08x.\n", __func__, offset);
            break;
    }

    return r;
}

static void s5l8702_lcd_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    S5L8702LcdState *s = S5L8702_LCD(opaque);

//    switch (offset) {
//    case LCD_CONFIG:
//        s->config = (uint32_t) val;
//        printf("s5l8702_lcd_write: LCD_CONFIG = 0x%08x\n", (uint32_t) val);
//        break;
//    case LCD_WCMD:
//        s->wcmd = (uint32_t) val;
//        printf("s5l8702_lcd_write: LCD_WCMD = 0x%08x\n", (uint32_t) val);
//
//        switch (s->wcmd) {
//            case 0x28: // DISPLAY_OFF
//                printf("s5l8702_lcd_write: DISPLAY_OFF\n");
//                break;
//            case 0x29: // DISPLAY_ON
//                printf("s5l8702_lcd_write: DISPLAY_ON\n");
//                break;
//            default:
//                qemu_log_mask(LOG_UNIMP, "%s: unimplemented lcd command (0x%08x)\n",
//                              __func__, s->wcmd);
//        }
//        break;
//    case LCD_STATUS:
//        s->status = (uint32_t) val;
//        printf("s5l8702_lcd_write: LCD_STATUS = 0x%08x\n", (uint32_t) val);
//        break;
//    case LCD_PHTIME:
//        s->phtime = (uint32_t) val;
//        printf("s5l8702_lcd_write: LCD_PHTIME = 0x%08x\n", (uint32_t) val);
//        break;
//    case LCD_WDATA:
//        s->wdata = (uint32_t) val;
//        printf("s5l8702_lcd_write: LCD_WDATA = 0x%08x\n", (uint32_t) val);
//        break;
//    default:
//        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write (offset 0x%04x, value 0x%08x)\n",
//                      __func__, (uint32_t) offset, (uint32_t) val);
//    }
    switch(offset) {
        case LCD_CONFIG:
            s->lcd_config = val;
            break;
        case LCD_WCMD:
            s->lcd_wcmd = val;
            // if(val < 0x2A || val > 0x2C) printf("LCD Got Command 0x%08x\n", s->lcd_wcmd);
            switch(s->lcd_wcmd) {
                case 0x04:
                    fifo8_reset(s->dbuff_buf);
                    fifo8_push(s->dbuff_buf, 0x00);
                    fifo8_push(s->dbuff_buf, 0x38);
                    fifo8_push(s->dbuff_buf, 0xB3);
                    fifo8_push(s->dbuff_buf, 0x71);
                    break;
                case 0x2c:
                    s->memcnt = 0;
                    s->address_latches = 0;
                    break;
                case 0x28:
                    printf("DISPLAY OFF\n");
                    break;
                case 0x29:
                    printf("DISPLAY ON\n");
                    break;
            }
            break;
        case LCD_RCMD:
            s->lcd_rcmd = val;
            break;
        case LCD_RDATA:
            s->lcd_rdata = val;
            if(val == 0) {
                if(fifo8_is_empty(s->dbuff_buf)) s->lcd_dbuff = 0;
                else s->lcd_dbuff = fifo8_pop(s->dbuff_buf) << 1;
            }
            break;
        case LCD_DBUFF:
            s->lcd_dbuff = val;
            break;
        case LCD_INTCON:
            s->lcd_intcon = val;
            break;
        case LCD_STATUS:
            s->lcd_status = val;
            break;
        case LCD_PHTIME:
            s->lcd_phtime = val;
            break;
        case LCD_WDATA:
            s->lcd_wdata = val;
            switch(s->lcd_wcmd) {
            case 0x2A:
                if(s->address_latches < 2) s->sc = (s->sc << 8) | val;
                else s->ec = (s->ec << 8) | val;
                s->address_latches++;
                if(s->address_latches == 4) {
                    s->address_latches = 0;
                    printf("LCD GOT 0x2A: sc=%04x ec=%04x\n", s->sc, s->ec);
                }
                break;
            case 0x2B:
                if(s->address_latches < 2) s->sp = (s->sp << 8) | val;
                else s->ep = (s->ep << 8) | val;
                s->address_latches++;
                if(s->address_latches == 4) {
                    s->address_latches = 0;
                    printf("LCD GOT 0x2B: sp=%04x ep=%04x\n", s->sp, s->ep);
                }

                break;
            case 0x2C:
                uint32_t address;
                // this simulates writing pixels as if we were a ILI9341. we start at the top left corner of the column and page defined by sc and sp
                // and write pixels until we reach the bottom right corner of the column and page defined by ec and ep. we keep track of the current
                // pixel we're on with s->memcnt and increment it every time we write a pixel. we use this to calculate the address of the pixel we're
                // writing to in the framebuffer. if we reach the end of the page (memcnt > ec - sc) we increment the page and reset the column.

                address = (s->sp * 320) + s->sc + s->memcnt;
                if(s->memcnt > s->ec - s->sc) {
                    s->sp++;
                    // s->sc = 0;
                    s->memcnt = 0;
                    address = (s->sp * 320) + s->sc + s->memcnt;
                }
                
                cpu_physical_memory_write(0xfe00000 + address * 2, &val, 2);
                s->invalidate = true;
                // printf("FB writing %08x to %08x\n", val, 0xfe00000 + address * 2);
                s->memcnt++;
                break;
            }
            break;
        default:
//            printf("%s: write invalid location 0x%08x.\n", __func__, offset);
            break;
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

    s->dbuff_buf = g_malloc0(sizeof(Fifo8));
    fifo8_create(s->dbuff_buf, 0x4);

    // initialize the lcd's internal registers (they're all just uint64_t's)
    s->lcd_regs = g_malloc0(sizeof(uint64_t) * 0xFF);

    // initialize the lcd's internal framebuffer
    s->framebuffer = g_malloc0(sizeof(uint16_t) * 320 * 240);
}

static void fb_invalidate_display(void *opaque)
{
    S5L8702LcdState *s = S5L8702_LCD(opaque);

    s->invalidate = true;
}

static void draw_line32_32(void *opaque, uint8_t *d, const uint8_t *s, int width, int deststep)
{
    uint8_t r, g, b;

    do {
        uint16_t v = lduw_le_p((void *) s);
        //printf("V: %d\n", *s);
        // convert 5-6-5 to 8-8-8
        // uint16_t v = ((uint16_t *) s)[0];
        r = (uint8_t)(((v & 0xF800) >> 11) << 3);
        g = (uint8_t)(((v & 0x7E0) >> 5) << 2);
        b = (uint8_t)(((v & 0x1F)) << 3);
        // if(r > 0 && r < 0xFF) printf("R: %d, G: %d, B: %d\n", r, g, b);
        ((uint32_t *) d)[0] = rgb_to_pixel32(r, g, b);
        s += 2;
        d += 4;
    } while (-- width != 0);
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

    drawfn draw_line;
    int src_width, dest_width;
    int height, first, last;
    int width, linesize;

    if (!s->con || !surface_bits_per_pixel(surface))
        return;

    dest_width = 4;
    draw_line = draw_line32_32;

    /* Resolution */
    first = last = 0;
    width = 320;
    height = 240;
    s->invalidate = 1;

    src_width =  2 * width;
    linesize = surface_stride(surface);

    if(s->invalidate) {
        framebuffer_update_memory_section(&s->fbsection, s->sysmem, 0xfe00000, height, src_width);
    }

    framebuffer_update_display(surface, &s->fbsection,
                               width, height,
                               src_width,       /* Length of source line, in bytes.  */
                               linesize,        /* Bytes between adjacent horizontal output pixels.  */
                               dest_width,      /* Bytes between adjacent vertical output pixels.  */
                               s->invalidate,
                               draw_line, NULL,
                               &first, &last);
    if (first >= 0) {
        dpy_gfx_update(s->con, 0, first, width, last - first + 1);
    }

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

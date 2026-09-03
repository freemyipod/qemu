/*
 * S5L8702 CLCD -- the display controller that scans overlay windows out of
 * DRAM. Distinct from s5l8702-lcd at 0x38300000, which is the command/data
 * interface to the panel itself; the EFI paints through that one pixel by
 * pixel, but RetailOS hands whole framebuffers to this block instead.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "sysemu/dma.h"
#include "hw/misc/s5l8702-clcd.h"
#include "trace.h"

#define REG_INDEX(offset) ((offset) / sizeof(uint32_t))

#define S5L8702_CLCD_CTRL           0x000
#define S5L8702_CLCD_CTRL_ENABLE    BIT(0)

#define S5L8702_CLCD_WIN(n)         (0x58 + (n) * 0x18)
#define S5L8702_CLCD_WIN_STRIDE     0x00
#define S5L8702_CLCD_WIN_CONFIG     0x04
#define S5L8702_CLCD_WIN_BASE       0x08
#define S5L8702_CLCD_WIN_SIZE       0x0c
#define S5L8702_CLCD_WIN_LINELEN    0x10
#define S5L8702_CLCD_WIN_POS        0x14

static uint32_t clcd_reg(S5L8702ClcdState *s, hwaddr offset) {
    return s->regs[REG_INDEX(offset)];
}

bool s5l8702_clcd_enabled(S5L8702ClcdState *s) {
    return s && (clcd_reg(s, S5L8702_CLCD_CTRL) & S5L8702_CLCD_CTRL_ENABLE);
}

// Bytes per pixel for a window CONFIG's format field, 0 if we cannot draw it.
static unsigned clcd_bytes_per_pixel(uint32_t config) {
    switch ((config >> 8) & 0xff) {
    case 2:
    case 3:
    case 4:
    case 5:
        return 2;
    case 6:
    case 7:
        return 4;
    default:
        // 0 and 1 are the 4bpp and 8bpp paletted formats; no palette here
        return 0;
    }
}

static uint32_t clcd_pixel(const uint8_t *src, unsigned bpp) {
    if (bpp == 4) {
        // Little-endian 0xAARRGGBB, i.e. B, G, R, A in memory
        return ((uint32_t)src[2] << 16) | ((uint32_t)src[1] << 8) | src[0];
    } else {
        uint16_t v = lduw_le_p(src);
        uint8_t r = ((v >> 11) & 0x1f) << 3;
        uint8_t g = ((v >> 5) & 0x3f) << 2;
        uint8_t b = (v & 0x1f) << 3;
        return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
}

static void clcd_draw_window(S5L8702ClcdState *s, unsigned n, uint32_t *dest, int width, int height) {
    hwaddr win = S5L8702_CLCD_WIN(n);
    uint32_t base = clcd_reg(s, win + S5L8702_CLCD_WIN_BASE);
    uint32_t size = clcd_reg(s, win + S5L8702_CLCD_WIN_SIZE);
    uint32_t pos = clcd_reg(s, win + S5L8702_CLCD_WIN_POS);
    uint32_t stride = clcd_reg(s, win + S5L8702_CLCD_WIN_STRIDE);
    unsigned bpp = clcd_bytes_per_pixel(clcd_reg(s, win + S5L8702_CLCD_WIN_CONFIG));
    int w = (size >> 16) & 0xffff;
    int h = size & 0xffff;
    int x = pos & 0xffff;
    int y;
    g_autofree uint8_t *line = NULL;

    if (!base || !w || !h || !bpp || !stride) {
        return;
    }

    // The vertical field is the margin below the window, not its top edge.
    y = height - h - (int)((pos >> 16) & 0xffff);

    if (x >= width || y >= height) return;
    if (w > width - x) w = width - x;
    if (h > height - y) h = height - y;
    if (w <= 0 || h <= 0) return;

    line = g_malloc((size_t)w * bpp);

    for (int row = 0; row < h; row++) {
        int dy = y + row;
        uint32_t *out;

        if (dy < 0) continue;
        
        if (dma_memory_read(s->as, base + (hwaddr)row * stride, line, (size_t)w * bpp, MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            return;
        }

        out = dest + (size_t)dy * width + x;
        for (int col = 0; col < w; col++) {
            out[col] = clcd_pixel(line + (size_t)col * bpp, bpp);
        }
    }
}

void s5l8702_clcd_composite(S5L8702ClcdState *s, uint32_t *dest, int width, int height) {
    memset(dest, 0, (size_t)width * height * sizeof(*dest));

    for (unsigned n = 0; n < S5L8702_CLCD_WINDOWS; n++) {
        clcd_draw_window(s, n, dest, width, height);
    }
}

static uint64_t s5l8702_clcd_read(void *opaque, hwaddr offset, unsigned size) {
    S5L8702ClcdState *s = S5L8702_CLCD(opaque);
    uint32_t val = s->regs[REG_INDEX(offset)];

    trace_s5l8702_clcd_read((uint32_t)offset, val);
    return val;
}

static void s5l8702_clcd_write(void *opaque, hwaddr offset, uint64_t value, unsigned size) {
    S5L8702ClcdState *s = S5L8702_CLCD(opaque);

    trace_s5l8702_clcd_write((uint32_t)offset, (uint32_t)value);
    s->regs[REG_INDEX(offset)] = (uint32_t)value;
}

static const MemoryRegionOps s5l8702_clcd_ops = {
    .read = s5l8702_clcd_read,
    .write = s5l8702_clcd_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void s5l8702_clcd_reset(DeviceState *dev) {
    S5L8702ClcdState *s = S5L8702_CLCD(dev);

    memset(s->regs, 0, sizeof(s->regs));
}

static void s5l8702_clcd_init(Object *obj) {
    S5L8702ClcdState *s = S5L8702_CLCD(obj);

    memory_region_init_io(&s->iomem, obj, &s5l8702_clcd_ops, s, TYPE_S5L8702_CLCD, S5L8702_CLCD_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void s5l8702_clcd_class_init(ObjectClass *klass, void *data) {
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = s5l8702_clcd_reset;
}

static const TypeInfo s5l8702_clcd_types[] = {
    {
        .name          = TYPE_S5L8702_CLCD,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_init = s5l8702_clcd_init,
        .instance_size = sizeof(S5L8702ClcdState),
        .class_init    = s5l8702_clcd_class_init,
    },
};
DEFINE_TYPES(s5l8702_clcd_types);

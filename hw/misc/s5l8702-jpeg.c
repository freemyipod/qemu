#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/s5l8702-jpeg.h"
#include <math.h>

#define JPEG_UNK1   0x60000

uint8_t zigzag[] = { 0, 1, 5, 6, 14, 15, 27, 28,
                     2, 4, 7, 13, 16, 26, 29, 42,
                     3, 8, 12, 17, 25, 30, 41, 43,
                     9, 11, 18, 24, 31, 40, 44, 53,
                     10, 19, 23, 32, 39, 45, 52, 54,
                     20, 22, 33, 38, 46, 51, 55, 60,
                     21, 34, 37, 47, 50, 56, 59, 61,
                     35, 36, 48, 49, 57, 58, 62, 63 };

static uint8_t clamp(double x) {
    if (x < 0) {
        return 0;
    } else if (x > 255) {
        return 255;
    } else {
        return x;
    }
}

static double idct_lookup[8][8][8][8];

static void s5l8702_jpeg_decode(EncodedMCU* mcu, uint32_t* qtable1, uint32_t* qtable2, uint8_t* yout, uint8_t* cbout, uint8_t* crout) {
    DecodedMCU* decoded = malloc(sizeof(DecodedMCU) * 300);
    double *yplane = malloc(sizeof(double) * 320 * 240);
    double *crplane = malloc(sizeof(double) * 160 * 120);
    double *cbplane = malloc(sizeof(double) * 160 * 120);

    int32_t dct_matrix[8][8];

    for(int i = 0; i < 300; i++) {
        // do the luminance blocks
        for (int data_unit_row = 0; data_unit_row < 2; data_unit_row++) {
            for (int data_unit_column = 0; data_unit_column < 2; data_unit_column++) {

                // dequantize
                for (int l = 0; l < 0x40; l++) {
                    dct_matrix[l / 8][l % 8] = __builtin_bswap32(mcu[i].lum[data_unit_row*2+data_unit_column].coeff[zigzag[l]]) * qtable1[l];
                }

                // perform IDCT
                for (int y = 0; y < 8; y++) {
                    for (int x = 0; x < 8; x++) {
                        double sum = 0;
                        for (int u = 0; u < 8; u++) {
                            for (int v = 0; v < 8; v++) {
                                sum += idct_lookup[y][x][u][v] * dct_matrix[u][v];
                            }
                        }

                        decoded[i].yplane[data_unit_column*8+y][data_unit_row*8+x] = round(sum/4 + 128);
                    }
                }
            }
        }

        // do the chrominance blocks
        // dequantize
        for (int l = 0; l < 0x40; l++) {
            dct_matrix[l / 8][l % 8] = __builtin_bswap32(mcu[i].chromb.coeff[zigzag[l]]) * qtable2[l];
        }

        // perform IDCT
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                double sum = 0;
                for (int u = 0; u < 8; u++) {
                    for (int v = 0; v < 8; v++) {
                        sum += idct_lookup[y][x][u][v] * dct_matrix[u][v];
                    }
                }

                // set four pixels
                decoded[i].cbplane[y][x] = round(sum/4 + 128);
            }
        }

        // dequantize
        for (int l = 0; l < 0x40; l++) {
            dct_matrix[l / 8][l % 8] = __builtin_bswap32(mcu[i].chromr.coeff[zigzag[l]]) * qtable2[l];
        }

        // perform IDCT
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                double sum = 0;
                for (int u = 0; u < 8; u++) {
                    for (int v = 0; v < 8; v++) {
                        sum += idct_lookup[y][x][u][v] * dct_matrix[u][v];
                    }
                }

                // set four pixels
                decoded[i].crplane[y][x] = round(sum/4 + 128);
            }
        }
    }

    // reconstruct the image
    for (int i = 0; i < 300; i++) {
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) {
                yplane[(i%20)*16 + (i/20) * 16 * 320 + (y)*320 + x] = decoded[i].yplane[x][y];
            }
        }
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                cbplane[(i%20)*8 + (i/20) * 8 * 160 + (y)*160 + x] = decoded[i].cbplane[x][y];
                crplane[(i%20)*8 + (i/20) * 8 * 160 + (y)*160 + x] = decoded[i].crplane[x][y];
            }
        }
    }

    for (int i = 0; i < 320 * 240; i++) {
        yout[i] = clamp(yplane[i]);
    }

    for (int i = 0; i < 160 * 120; i++) {
        cbout[i] = clamp(cbplane[i]);
        crout[i] = clamp(crplane[i]);
    }

    for (int i = 0; i < 320 * 240; i += 4) {
        // __builtin_bswap32 the pixels to reverse columns of 4 pixels in place in each channel
        uint32_t *y32 = (uint32_t *)&yout[i];
        *y32 = __builtin_bswap32(*y32);
    }

    for (int i = 0; i < 160 * 120; i += 4) {
        uint32_t *cb32 = (uint32_t *)&cbout[i];
        uint32_t *cr32 = (uint32_t *)&crout[i];
        *cb32 = __builtin_bswap32(*cb32);
        *cr32 = __builtin_bswap32(*cr32);
    }

    free(decoded);
    free(yplane);
    free(cbplane);
    free(crplane);
}

static uint64_t s5l8702_jpeg_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    const S5L8702JpegState *s = S5L8702_JPEG(opaque);
    uint32_t r = 0;

    switch (offset) {
    case JPEG_UNK1:
        r = 0xFFFFFFFF;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read (offset 0x%04x)\n",
                      __func__, (uint32_t) offset);
    }

    return r;
}

static void s5l8702_jpeg_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    S5L8702JpegState *s = S5L8702_JPEG(opaque);

//    switch (offset) {
//    default:
//        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write (offset 0x%04x, value 0x%08x)\n",
//                      __func__, (uint32_t) offset, (uint32_t) val);
//    }

    s->regs[offset / 4] = val;

    switch (offset) {
        case JPEG_REG_QTABLE1 ... JPEG_REG_QTABLE1 + JPEG_QTABLE_LEN:
            s->qtable1[(offset - JPEG_REG_QTABLE1)/4] = val;
            break;
        case JPEG_REG_QTABLE2 ... JPEG_REG_QTABLE2 + JPEG_QTABLE_LEN:
            s->qtable2[(offset - JPEG_REG_QTABLE2)/4] = val;
            break;
        case JPEG_REG_CTRL:
            EncodedMCU* mcu = malloc(sizeof(EncodedMCU) * 300);
            uint8_t *yplane = malloc(sizeof(uint8_t) * 320 * 240);
            uint8_t *cbplane = malloc(sizeof(uint8_t) * 320 * 240);
            uint8_t *crplane = malloc(sizeof(uint8_t) * 320 * 240);

            MemTxResult result = address_space_read(s->nsas, s->regs[JPEG_REG_COEFF_BLOCKS/4] ^ 0x80000000, MEMTXATTRS_UNSPECIFIED, mcu, sizeof(EncodedMCU) * 300);

            s5l8702_jpeg_decode(mcu, s->qtable1, s->qtable2, yplane, cbplane, crplane);

            address_space_write(s->nsas, (s->regs[JPEG_REG_OUT_CRPLANE/4] + 0x10000) ^ 0x80000000, MEMTXATTRS_UNSPECIFIED, yplane, 320 * 240);
            address_space_write(s->nsas, (s->regs[JPEG_REG_OUT_CRPLANE/4] + 0x10000 + 0x12C00) ^ 0x80000000, MEMTXATTRS_UNSPECIFIED, cbplane, 160 * 120);
            address_space_write(s->nsas, (s->regs[JPEG_REG_OUT_CRPLANE/4] + 0x10000 + 0x12C00 + 0x4B00) ^ 0x80000000, MEMTXATTRS_UNSPECIFIED, crplane, 160 * 120);

            free(mcu);
            free(yplane);
            free(cbplane);
            free(crplane);
            break;

        default:
            break;
    }
}

static const MemoryRegionOps s5l8702_jpeg_ops = {
    .read = s5l8702_jpeg_read,
    .write = s5l8702_jpeg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void s5l8702_jpeg_reset(DeviceState *dev)
{
    S5L8702JpegState *s = S5L8702_JPEG(dev);

    printf("s5l8702_jpeg_reset\n");

    // precompute the IDCT lookup table
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            for (int u = 0; u < 8; u++) {
                for (int v = 0; v < 8; v++) {
                    double cu = (u == 0) ? (1 / sqrt(2)) : 1;
                    double cv = (v == 0) ? (1 / sqrt(2)) : 1;
                    idct_lookup[y][x][u][v] = cu * cv * cos(((2 * x + 1) * u * M_PI) / 16) * cos(((2 * y + 1) * v * M_PI) / 16);
                }
            }
        }
    }

    /* Reset registers */

}

static void s5l8702_jpeg_init(Object *obj)
{
    S5L8702JpegState *s = S5L8702_JPEG(obj);

    printf("s5l8702_jpeg_init\n");

    /* Memory mapping */
    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_jpeg_ops, s, TYPE_S5L8702_JPEG, S5L8702_JPEG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static void s5l8702_jpeg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = s5l8702_jpeg_reset;
}

static const TypeInfo s5l8702_jpeg_types[] = {
    {
        .name = TYPE_S5L8702_JPEG,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_init = s5l8702_jpeg_init,
        .instance_size = sizeof(S5L8702JpegState),
        .class_init = s5l8702_jpeg_class_init,
    },
};
DEFINE_TYPES(s5l8702_jpeg_types);

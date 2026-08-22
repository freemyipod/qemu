#ifndef HW_MISC_S5L8702_PRNG_H
#define HW_MISC_S5L8702_PRNG_H

#include "qom/object.h"
#include "hw/sysbus.h"

#define TYPE_S5L8702_PRNG "s5l8702-prng"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702PrngState, S5L8702_PRNG)

#define S5L8702_PRNG_BASE 0x3C100000
#define S5L8702_PRNG_SIZE 0x00100000

/* Register offsets. */
#define S5L8702_PRNG_STATUS 0x0
#define S5L8702_PRNG_DATA   0x4
#define S5L8702_PRNG_SEED   0x8

// Firmware polls the low three bits of STATUS and treats "any set" as data-ready (`while ((*status & 7) == 0);`), so report all three.
#define S5L8702_PRNG_STATUS_READY 0x7

// Value the generator starts from when firmware never writes SEED. AUPD is exactly that case, so this must be non-zero or xorshift would be stuck at 0.
#define S5L8702_PRNG_DEFAULT_SEED 0x2545f491

struct S5L8702PrngState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    uint32_t state;
};

#endif /* HW_MISC_S5L8702_PRNG_H */

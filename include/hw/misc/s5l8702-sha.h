#ifndef HW_MISC_S5L8702_SHA_H
#define HW_MISC_S5L8702_SHA_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include <openssl/sha.h>

#define TYPE_S5L8702_SHA    "s5l8702-sha"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702ShaState, S5L8702_SHA)

#define S5L8702_SHA_BASE    0x38000000
#define S5L8702_SHA_SIZE    0x00100000

#define S5L8702_SHA_NUM_REGS    (S5L8702_SHA_SIZE / sizeof(uint32_t))

#define SHA1_BUFFER_SIZE 1024 * 1024

#define SHA_CONFIG         0x0
#define SHA_RESET          0x4
#define SHA_MEMORY_MODE    0x80 // whether we read from the memory
#define SHA_MEMORY_START   0x84
#define SHA_INSIZE         0x8c

struct S5L8702ShaState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint32_t config;
    uint32_t memory_start;
    uint32_t memory_mode;
    uint32_t insize;
    uint8_t buffer[SHA1_BUFFER_SIZE];
    uint32_t hw_buffer[0x10]; // hardware buffer
    uint32_t buffer_ind;
    uint8_t hashout[0x14];
    bool hw_buffer_dirty;
    bool hash_computed;
    SHA_CTX ctx;
};

#endif /* HW_MISC_S5L8702_SHA_H */

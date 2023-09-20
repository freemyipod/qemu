#ifndef HW_MISC_S5L8702_SHA_H
#define HW_MISC_S5L8702_SHA_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include <openssl/sha.h>

#define TYPE_S5L8702_SHA    "s5l8702-sha"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702ShaState, S5L8702_SHA)

#define S5L8702_SHA_BASE    0x38000000
#define S5L8702_SHA_SIZE    0x00100000

#define SHA1_BUFFER_SIZE 1024 * 1024

struct S5L8702ShaState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;

    uint8_t buffer[SHA1_BUFFER_SIZE];
    size_t buffer_len;

    uint32_t config;
    uint32_t inbuf[16];

    uint8_t outbuf[0x14];
    bool hash_computed;
};

#endif /* HW_MISC_S5L8702_SHA_H */

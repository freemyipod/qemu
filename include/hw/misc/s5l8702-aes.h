#ifndef HW_MISC_S5L8702_AES_H
#define HW_MISC_S5L8702_AES_H

#include "qom/object.h"
#include "hw/sysbus.h"
#include <openssl/aes.h>

#define TYPE_S5L8702_AES    "s5l8702-aes"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702AesState, S5L8702_AES)

#define S5L8702_AES_BASE    0x38C00000
#define S5L8702_AES_SIZE    0x00100000

#define S5L8702_AES_NUM_REGS    (S5L8702_AES_SIZE / sizeof(uint32_t))

#define AES_128_CBC_BLOCK_SIZE 64
#define AES_CONTROL 0x0
#define AES_GO 0x4
#define AES_UNKREG0 0x8
#define AES_STATUS 0xC
#define AES_UNKREG1 0x10
#define AES_KEYLEN 0x14
#define AES_INSIZE 0x24
#define AES_INADDR 0x28
#define AES_OUTSIZE 0x18
#define AES_OUTADDR 0x20
#define AES_AUXSIZE 0x2C
#define AES_AUXADDR 0x30
#define AES_SIZE3 0x34
#define AES_KEY_REG 0x4C
#define AES_TYPE 0x6C
#define AES_IV_REG 0x74
#define AES_KEYSIZE 0x20
#define AES_IVSIZE 0x10

typedef enum AESKeyType {
    AESCustom = 0,
    AESGID = 1,
    AESUID = 2
} AESKeyType;

typedef enum AESKeyLen {
    AES128 = 0,
    AES192 = 1,
    AES256 = 2
} AESKeyLen;

typedef struct S5L8702AesState
{
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    AES_KEY decryptKey;
	uint32_t ivec[4];
	uint32_t insize;
	uint32_t inaddr;
	uint32_t outsize;
	uint32_t outaddr;
	uint32_t auxaddr;
	uint32_t keytype;
	uint32_t status;
	uint32_t ctrl;
	uint32_t unkreg0;
	uint32_t unkreg1;
	uint32_t keylen;
	uint32_t custkey[8]; 
} S5L8702AesState;

#endif /* HW_MISC_S5L8702_AES_H */

#ifndef HW_MISC_S5L8702_BUSCON_H
#define HW_MISC_S5L8702_BUSCON_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_S5L8702_BUSCON "s5l8702-buscon"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702BusConState, S5L8702_BUSCON)

#define S5L8702_BUSCON_BASE 0x3E000000
#define S5L8702_BUSCON_SIZE 0x00001000

struct S5L8702BusConState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    /*
     * The two candidate views of address 0, owned by the SoC. Exactly one is
     * enabled at a time; REMAP picks which.
     */
    MemoryRegion *brom_alias;
    MemoryRegion *sram_alias;

    uint32_t remap;
};

/*
 * Point address 0 at IRAM0 (sram = true) or back at the BootROM. Callable from
 * any device that owns a register controlling the remap; on this SoC that is
 * both this block and the MIU's MIUCON.
 */
void s5l8702_buscon_select(S5L8702BusConState *s, bool sram);

#endif /* HW_MISC_S5L8702_BUSCON_H */

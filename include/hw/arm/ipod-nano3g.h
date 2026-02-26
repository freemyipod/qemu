#ifndef HW_ARM_IPOD_NANO3G_H
#define HW_ARM_IPOD_NANO3G_H

#include "qom/object.h"
#include "hw/arm/boot.h"
#include "hw/intc/arm_gic.h"
#include "target/arm/cpu.h"
#include "sysemu/block-backend.h"
#include "hw/arm/s5l8702.h"
#include "hw/misc/pcf5063x.h"

#define TYPE_IPOD_NANO3G_MACHINE   MACHINE_TYPE_NAME("ipod-nano3g")
OBJECT_DECLARE_SIMPLE_TYPE(IpodNano3gState, IPOD_NANO3G_MACHINE)

struct IpodNano3gState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    S5L8702State soc;
    MemoryRegion dram;
    MemoryRegion dram_alias;
    Pcf5063xState pcf5063x;

    char *bootrom_path;
    char *nand_path;
};

#endif /* HW_ARM_IPOD_NANO3G_H */

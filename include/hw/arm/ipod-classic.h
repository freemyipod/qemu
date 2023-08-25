#ifndef HW_ARM_IPOD_CLASSIC_H
#define HW_ARM_IPOD_CLASSIC_H

#include "qom/object.h"
#include "hw/arm/boot.h"
#include "hw/intc/arm_gic.h"
#include "target/arm/cpu.h"
#include "sysemu/block-backend.h"
#include "hw/arm/s5l8702.h"

#define TYPE_IPOD_CLASSIC_MACHINE   MACHINE_TYPE_NAME("ipod-classic")
OBJECT_DECLARE_SIMPLE_TYPE(IpodClassicState, IPOD_CLASSIC_MACHINE)

struct IpodClassicState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    S5L8702State soc;
    MemoryRegion dram;

    char *bootrom_path;
    char *nand_path;
    char *bootloader_path;
};

#endif /* HW_ARM_IPOD_CLASSIC_H */

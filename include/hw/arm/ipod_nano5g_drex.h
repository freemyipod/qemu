#ifndef IPOD_NANO5G_DREX_H
#define IPOD_NANO5G_DREX_H

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "hw/clock.h"

#define TYPE_IPOD_NANO5G_DREX                "ipodnano5g.drex"
OBJECT_DECLARE_SIMPLE_TYPE(IPodNano5GDREXState, IPOD_NANO5G_DREX)

typedef struct IPodNano5GDREXState
{
    SysBusDevice busdev;
    MemoryRegion iomem;
} IPodNano5GDREXState;

#endif
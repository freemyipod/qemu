#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/arm/boot.h"
#include "hw/registerfields.h"
#include "qemu/error-report.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "qom/object.h"
#include "hw/arm/ipod-nano3g.h"
#include "hw/qdev-properties.h"
#include "trace.h"

static char *ipod_nano3g_get_bootrom_path(Object *obj, Error **errp)
{
    IpodNano3gState *s = IPOD_NANO3G_MACHINE(obj);
    return g_strdup(s->bootrom_path);
}

static void ipod_nano3g_set_bootrom_path(Object *obj, const char *value, Error **errp)
{
    IpodNano3gState *s = IPOD_NANO3G_MACHINE(obj);
    g_free(s->bootrom_path);
    s->bootrom_path = g_strdup(value);
}

static char *ipod_nano3g_get_nand_path(Object *obj, Error **errp)
{
    IpodNano3gState *s = IPOD_NANO3G_MACHINE(obj);
    return g_strdup(s->nand_path);
}

static void ipod_nano3g_set_nand_path(Object *obj, const char *value, Error **errp)
{
    IpodNano3gState *s = IPOD_NANO3G_MACHINE(obj);
    g_free(s->nand_path);
    s->nand_path = g_strdup(value);
}

static void ipod_nano3g_init(Object *obj)
{
    MachineState *machine = MACHINE(obj);
    IpodNano3gState *s = IPOD_NANO3G_MACHINE(obj);

    trace_ipod_nano3g_init();

    if (!object_property_add_str(obj, "bootrom", ipod_nano3g_get_bootrom_path, ipod_nano3g_set_bootrom_path)) {
        error_report("ipod_nano3g_init: failed to add bootrom property\n");
        exit(1);
    }

    if (!object_property_add_str(obj, "nand-path", ipod_nano3g_get_nand_path, ipod_nano3g_set_nand_path)) {
        error_report("ipod_nano3g_init: failed to add nand-path property\n");
        exit(1);
    }
}

static void ipod_nano3g_key_event(void *opaque, int keycode) {
    S5L8702State *soc = (S5L8702State *)opaque;
    S5L8702GpioState *s = &soc->gpio;

    switch(keycode) {
        case 28:
            trace_ipod_nano3g_key_event("select pressed");
            s->clickwheel_select_pressed = 1;
            break;
        case 156:
            trace_ipod_nano3g_key_event("select released");
            s->clickwheel_select_pressed = 0;
            break;
        case 72:
            trace_ipod_nano3g_key_event("menu pressed");
            s->clickwheel_menu_pressed = 1;
            break;
        case 200:
            trace_ipod_nano3g_key_event("menu released");
            s->clickwheel_menu_pressed = 0;
            break;
        case 80:
            trace_ipod_nano3g_key_event("play pressed");
            s->clickwheel_play_pressed = 1;
            break;
        case 208:
            trace_ipod_nano3g_key_event("play released");
            s->clickwheel_play_pressed = 0;
            break;
        case 75:
            trace_ipod_nano3g_key_event("prev pressed");
            s->clickwheel_prev_pressed = 1;
            break;
        case 203:
            trace_ipod_nano3g_key_event("prev released");
            s->clickwheel_prev_pressed = 0;
            break;
        case 77:
            trace_ipod_nano3g_key_event("next pressed");
            s->clickwheel_next_pressed = 1;
            break;
        case 205:
            trace_ipod_nano3g_key_event("next released");
            s->clickwheel_next_pressed = 0;
            break;
        default:
            return; /* unrecognised key – do not notify clickwheel */
    }

    /* Notify the dedicated clickwheel controller peripheral */
    qemu_irq_pulse(qdev_get_gpio_in_named(DEVICE(&soc->clickwheel), "button-update", 0));
}

static void ipod_nano3g_machine_init(MachineState *machine)
{
    IpodNano3gState *s = IPOD_NANO3G_MACHINE(machine);

    trace_ipod_nano3g_machine_init();

    /* BIOS is not supported by this board */
    if (machine->firmware) {
        error_report("BIOS not supported for this machine");
        exit(1);
    }

    /* Only allow ARM926 for this board */
    if (strcmp(machine->cpu_type, ARM_CPU_TYPE_NAME("arm926")) != 0) {
        error_report("This board can only be used with arm926 CPU");
        exit(1);
    }

    /* This board has fixed size RAM (32MiB) */
    if (machine->ram_size != 32 * MiB) {
        error_report("This machine can only be used with 32MiB RAM");
        exit(1);
    }

    /* Only allow 1 CPU for this board */
    if (machine->smp.cpus != 1) {
        error_report("This machine can only be used with 1 CPU");
        exit(1);
    }

    if (!s->bootrom_path) {
        error_report("bootrom property not set");
        exit(1);
    }

    /* Initialize s5l8702 soc */
    object_initialize_child(OBJECT(s), "soc", &s->soc, TYPE_S5L8702);
    if (s->nand_path) {
        s->soc.nand.nand_path = g_strdup(s->nand_path);
    }
    sysbus_realize(SYS_BUS_DEVICE(&s->soc), &error_fatal);

    /* DRAM */
    memory_region_init_ram(&s->dram, OBJECT(s), "dram", machine->ram_size, &error_fatal);
    memory_region_add_subregion(get_system_memory(), S5L8702_DRAM_BASE_ADDR, &s->dram);

    memory_region_init_alias(&s->dram_alias, OBJECT(s), "dram-alias", &s->dram, 0, machine->ram_size);
    memory_region_add_subregion(get_system_memory(), 0x88000000, &s->dram_alias);

    /* Connect an SPI flash to SPI0 */
    DeviceState *flash_dev = qdev_new("sst25vf080b"); // According to https://freemyipod.org/wiki/Nano3g_3G
    DriveInfo *flash_info = drive_get_by_index(IF_MTD, 0);
    if (!flash_info) {
        error_report("NOR image not found");
        exit(1);
    }

    trace_ipod_nano3g_nor_loaded();
    qdev_prop_set_drive(flash_dev, "drive", blk_by_legacy_dinfo(flash_info));
    qdev_realize_and_unref(flash_dev, BUS(s->soc.spi[0].spi), &error_fatal);

    qemu_irq flash_cs = qdev_get_gpio_in_named(flash_dev, SSI_GPIO_CS, 0);
    qdev_connect_gpio_out(DEVICE(&s->soc.gpio), 0, flash_cs);
    
    /* PCF5063x */
    // object_initialize_child(OBJECT(s), "pcf5063x", &s->pcf5063x, TYPE_PCF5063X);
    i2c_slave_create_simple(s->soc.i2c[0].bus, TYPE_PCF5063X, 0x73);

    /* Read the bootrom, copy it to memory and execute it */
    uint8_t *bootrom = NULL;
    size_t bootrom_size = 0;
    if (g_file_get_contents(s->bootrom_path, (char **) &bootrom, &bootrom_size, NULL)) {
        trace_ipod_nano3g_bootrom_read(s->bootrom_path);
        AddressSpace *nsas = cpu_get_address_space(CPU(&s->soc.cpu), ARMASIdx_NS);
        address_space_write(nsas, 0x20000000, MEMTXATTRS_UNSPECIFIED, bootrom, bootrom_size);
        trace_ipod_nano3g_bootrom_copied(bootrom_size);
    } else {
        error_report("Failed to read bootrom from %s", s->bootrom_path);
        exit(1);
    }

    qemu_add_kbd_event_handler(ipod_nano3g_key_event, &s->soc);

    // HACK to get into diagnostic mode
    s->soc.gpio.clickwheel_select_pressed = 1;
    s->soc.gpio.clickwheel_menu_pressed = 1;
}

static void ipod_nano3g_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->init = ipod_nano3g_machine_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm926");
    mc->default_ram_size = 32 * MiB;
    mc->default_cpus = 1;
    mc->desc = "iPod Nano 3rd Generation (ARM926EJ-S)";
};

static const TypeInfo ipod_nano3g_types[] = {
    {
        .name = TYPE_IPOD_NANO3G_MACHINE,
        .parent = TYPE_MACHINE,
        .instance_size = sizeof(IpodNano3gState),
        .instance_init = ipod_nano3g_init,
        .class_init = ipod_nano3g_class_init,
    },
};
DEFINE_TYPES(ipod_nano3g_types)

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "cpu.h"
#include "exec/address-spaces.h"
#include "hw/boards.h"
#include "hw/qdev-core.h"
#include "hw/arm/s5l8702.h"
#include "hw/misc/unimp.h"

#define S5L8702_LCD_BASE    0x38300000
#define S5L8702_JPEG_BASE   0x39600000
#define S5L8702_DMA0_BASE   0x38200000
#define S5L8702_ATA_BASE    0x38700000

#define FRAMEBUFFER_MEM_BASE 0xfe00000

static uint32_t align_64k_high(uint32_t addr) {
    return (addr + 0xffffull) & ~0xffffull;
}
static void allocate_ram(MemoryRegion *top, const char *name, uint32_t addr, uint32_t size) {
    MemoryRegion *sec = g_new(MemoryRegion, 1);
    memory_region_init_ram(sec, NULL, name, size, &error_fatal);
    memory_region_add_subregion(top, addr, sec);
}

static void s5l8702_init(Object *obj)
{
    S5L8702State *s = S5L8702(obj);

    printf("s5l8702_init\n");

    object_initialize_child(obj, "cpu", &(s->cpu), ARM_CPU_TYPE_NAME("arm926"));

    for (uint32_t i = 0; i < ARRAY_SIZE(s->vic); i++) {
        object_initialize_child(obj, "vic[*]", &s->vic[i], TYPE_PL192);
    }

    /* PCLK */
    object_initialize_child(obj, "pclk", &s->pclk, TYPE_CLOCK);
    clock_setup_canonical_path(&s->pclk);
    clock_set_hz(&s->pclk, 121500000); // 121.5MHz?

    /* ECLK */
    object_initialize_child(obj, "eclk", &s->eclk, TYPE_CLOCK);
    clock_setup_canonical_path(&s->eclk);
    clock_set_hz(&s->eclk, 12000000); // 12 MHz

    /* EXTCLK */
    object_initialize_child(obj, "extclk0", &s->extclk0, TYPE_CLOCK);
    clock_setup_canonical_path(&s->extclk0);
    object_initialize_child(obj, "extclk1", &s->extclk1, TYPE_CLOCK);
    clock_setup_canonical_path(&s->extclk1);
    
    object_initialize_child(obj, "clk", &s->clk, TYPE_S5L8702_CLK);
    object_initialize_child(obj, "aes", &s->aes, TYPE_S5L8702_AES);
    object_initialize_child(obj, "sha", &s->sha, TYPE_S5L8702_SHA);
    object_initialize_child(obj, "gpio", &s->gpio, TYPE_S5L8702_GPIO);

    for (uint32_t i = 0; i < ARRAY_SIZE(s->spi); i++) {
        object_initialize_child(obj, "spi[*]", &s->spi[i], TYPE_S5L8702_SPI);
    }

    for (uint32_t i = 0; i < ARRAY_SIZE(s->i2c); i++) {
        object_initialize_child(obj, "i2c[*]", &s->i2c[i], TYPE_S5L8702_I2C);
    }

    object_initialize_child(obj, "timer", &s->timer, TYPE_S5L8702_TIMER);
    object_initialize_child(obj, "lcd", &s->lcd, TYPE_S5L8702_LCD);
    object_initialize_child(obj, "jpeg", &s->jpeg, TYPE_S5L8702_JPEG);

    for (uint32_t i = 0; i < ARRAY_SIZE(s->dma); i++) {
        object_initialize_child(obj, "dma[*]", &s->dma[i], TYPE_PL080);
    }

    object_initialize_child(obj, "ata", &s->ata, TYPE_S5L8702_ATA);
}

static void s5l8702_realize(DeviceState *dev, Error **errp)
{
    S5L8702State *s = S5L8702(dev);
    MemoryRegion *system_memory = get_system_memory();

    // allocate ram
    MemoryRegion *sec = g_new(MemoryRegion, 1);
    memory_region_init_ram(sec, NULL, "ram", 0x2000000, &error_fatal);
    memory_region_add_subregion(system_memory, 0x8000000, sec);

    MemoryRegion *iomem = g_new(MemoryRegion, 1);
    memory_region_init_alias(iomem, NULL, "himem", sec, 0, 0x2000000);
    memory_region_add_subregion(system_memory, 0x88000000, iomem);

    printf("s5l8702_realize\n");

    qdev_realize(DEVICE(&s->cpu), NULL, &error_fatal);

    /* VIC */
    for (int i = 0; i < ARRAY_SIZE(s->vic); i++) {
        sysbus_realize(SYS_BUS_DEVICE(&s->vic[i]), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(&s->vic[i]), 0, S5L8702_VIC_BASE_ADDR + (i * 0x1000));
        // sysbus_connect_irq(SYS_BUS_DEVICE(&s->vic[i]), 0, cpu_irq[0]);
        // sysbus_connect_irq(SYS_BUS_DEVICE(&s->vic[i]), 1, cpu_fiq[0]);
    }

    /* CLK */
    sysbus_realize(SYS_BUS_DEVICE(&s->clk), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->clk), 0, S5L8702_CLK_BASE_ADDR);

    /* AES */
    sysbus_realize(SYS_BUS_DEVICE(&s->aes), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->aes), 0, S5L8702_AES_BASE);

    /* SHA */
    sysbus_realize(SYS_BUS_DEVICE(&s->sha), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->sha), 0, S5L8702_SHA_BASE);

    /* GPIO */
    sysbus_realize(SYS_BUS_DEVICE(&s->gpio), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->gpio), 0, S5L8702_GPIO_BASE);

    /* SPI */
    for (uint32_t i = 0; i < ARRAY_SIZE(s->spi); i++) {
        sysbus_realize(SYS_BUS_DEVICE(&s->spi[i]), &error_fatal);
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi[0]), 0, S5L8702_SPI0_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi[1]), 0, S5L8702_SPI1_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->spi[2]), 0, S5L8702_SPI2_BASE);

    /* I2C */
    for (uint32_t i = 0; i < ARRAY_SIZE(s->i2c); i++) {
        sysbus_realize(SYS_BUS_DEVICE(&s->i2c[i]), &error_fatal);
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->i2c[0]), 0, S5L8702_I2C0_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->i2c[1]), 0, S5L8702_I2C1_BASE);

    /* Timer */
    s->timer.pclk = &s->pclk;
    s->timer.eclk = &s->eclk;
    s->timer.extclk0 = &s->extclk0;
    s->timer.extclk1 = &s->extclk1;
    sysbus_realize(SYS_BUS_DEVICE(&s->timer), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->timer), 0, S5L8702_TIMER_BASE);

    /* LCD */
    s->lcd.sysmem = get_system_memory();
    s->lcd.nsas = cpu_get_address_space(CPU(&s->cpu), ARMASIdx_NS);
    allocate_ram(s->lcd.sysmem, "framebuffer", FRAMEBUFFER_MEM_BASE, align_64k_high(4 * 320 * 480));
    uint8_t stuff[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    address_space_rw(s->lcd.nsas, FRAMEBUFFER_MEM_BASE, MEMTXATTRS_UNSPECIFIED, (uint8_t *)stuff, 16, 1);
    sysbus_realize(SYS_BUS_DEVICE(&s->lcd), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->lcd), 0, S5L8702_LCD_BASE);

    /* JPEG */
    s->jpeg.sysmem = get_system_memory();
    s->jpeg.nsas = cpu_get_address_space(CPU(&s->cpu), ARMASIdx_NS);
    sysbus_realize(SYS_BUS_DEVICE(&s->jpeg), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->jpeg), 0, S5L8702_JPEG_BASE);

    /* DMA */
    for (uint32_t i = 0; i < ARRAY_SIZE(s->dma); i++) {
        object_property_set_link(OBJECT(&s->dma[i]), "downstream", OBJECT(get_system_memory()), &error_fatal);
        sysbus_realize(SYS_BUS_DEVICE(&s->dma[i]), &error_fatal);
    }
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->dma[0]), 0, S5L8702_DMA0_BASE);
    //sysbus_mmio_map(SYS_BUS_DEVICE(&s->dma[1]), 0, S5L8702_DMA1_BASE);

    /* ATA */
    sysbus_realize(SYS_BUS_DEVICE(&s->ata), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(&s->ata), 0, S5L8702_ATA_BASE);

    /* BootROM */
    memory_region_init_ram(&s->brom, OBJECT(dev), "s5l8702.bootrom", S5L8702_BOOTROM_SIZE, &error_fatal);
    memory_region_add_subregion(system_memory, S5L8702_BOOTROM_BASE_ADDR, &s->brom);
    memory_region_init_alias(&s->brom_alias, OBJECT(dev), "s5l8702.bootrom-alias", &s->brom, 0, S5L8702_BOOTROM_SIZE);
    memory_region_add_subregion(system_memory, S5L8702_BASE_BOOT_ADDR, &s->brom_alias);

    /* IRAM0 */
    memory_region_init_ram(&s->iram0, OBJECT(dev), "s5l8702.iram0", S5L8702_IRAM0_SIZE, &error_fatal);
    memory_region_add_subregion(system_memory, S5L8702_IRAM0_BASE_ADDR, &s->iram0);

    /* IRAM1 */
    memory_region_init_ram(&s->iram1, OBJECT(dev), "s5l8702.iram1", S5L8702_IRAM1_SIZE, &error_fatal);
    memory_region_add_subregion(system_memory, S5L8702_IRAM1_BASE_ADDR, &s->iram1);

    create_unimplemented_device("unimplemented-mem", 0x0, 0xFFFFFFFF);
    create_unimplemented_device("wdt", 0x3c800000, 0x100000);
    create_unimplemented_device("miu", 0x38100000, 0x100000);
    create_unimplemented_device("chipid", 0x3D100000, 0x100000); // I think so, at least
    // 👇🏻 https://github.com/Rockbox/rockbox/blob/ed369e1d475658eccb5eb2221d757e7d66796e90/firmware/target/arm/s5l8702/clocking-s5l8702.h#L216
    create_unimplemented_device("sm1_div", 0x38501000, 0x04);
    create_unimplemented_device("phy", 0x3c400000, 0x100000);
    create_unimplemented_device("vic", 0x38E00000, 0x100000);
    create_unimplemented_device("unknown-dev-1", 0x39a00000, 0x100000);
}

static void s5l8702_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    printf("s5l8702_class_init\n");

    dc->realize = s5l8702_realize;
}

static const TypeInfo s5l8702_types[] = {
    {
        .name = TYPE_S5L8702,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(S5L8702State),
        .instance_init = s5l8702_init,
        .class_init = s5l8702_class_init,
    },
};
DEFINE_TYPES(s5l8702_types);

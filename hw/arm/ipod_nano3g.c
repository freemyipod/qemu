#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu-common.h"
#include "hw/arm/boot.h"
#include "exec/address-spaces.h"
#include "hw/misc/unimp.h"
#include "hw/irq.h"
#include "sysemu/sysemu.h"
#include "sysemu/reset.h"
#include "hw/platform-bus.h"
#include "hw/block/flash.h"
#include "hw/qdev-clock.h"
#include "hw/arm/ipod_nano3g.h"
#include "hw/arm/exynos4210.h"
#include "hw/dma/pl080.h"
#include <openssl/aes.h>

static uint64_t prng_workaround_read(void *opaque, hwaddr addr, unsigned size)
{
    switch (addr) {
    case 0:
        return 1<<2;
    default:
        return 0;
    }
}

static void prng_workaround_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
}

static const MemoryRegionOps prng_workaround_ops = {
    .read = prng_workaround_read,
    .write = prng_workaround_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t pl301_workaround_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pl301_workaround_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{
}

static const MemoryRegionOps pl301_workaround_ops = {
    .read = pl301_workaround_read,
    .write = pl301_workaround_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t tvout_workaround_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void tvout_workaround_write(void *opaque, hwaddr addr, uint64_t value, unsigned size)
{

}

static const MemoryRegionOps tvout_workaround_ops = {
    .read = tvout_workaround_read,
    .write = tvout_workaround_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void allocate_ram(MemoryRegion *top, const char *name, uint32_t addr, uint32_t size)
{
        MemoryRegion *sec = g_new(MemoryRegion, 1);
        memory_region_init_ram(sec, NULL, name, size, &error_fatal);
        memory_region_add_subregion(top, addr, sec);
}

static uint32_t align_64k_high(uint32_t addr)
{
    return (addr + 0xffffull) & ~0xffffull;
}

static void ipod_nano3g_cpu_setup(MachineState *machine, MemoryRegion **sysmem, ARMCPU **cpu, AddressSpace **nsas)
{
    Object *cpuobj = object_new(machine->cpu_type);
    *cpu = ARM_CPU(cpuobj);
    CPUState *cs = CPU(*cpu);

    *sysmem = get_system_memory();

    object_property_set_link(cpuobj, "memory", OBJECT(*sysmem), &error_abort);

    object_property_set_bool(cpuobj, "has_el3", false, NULL);

    object_property_set_bool(cpuobj, "has_el2", false, NULL);

    object_property_set_bool(cpuobj, "realized", true, &error_fatal);

    *nsas = cpu_get_address_space(cs, ARMASIdx_NS);

    object_unref(cpuobj);
}

static void ipod_nano3g_cpu_reset(void *opaque)
{
    IPodNano3GMachineState *nms = IPOD_NANO3G_MACHINE((MachineState *)opaque);
    ARMCPU *cpu = nms->cpu;
    CPUState *cs = CPU(cpu);

    cpu_reset(cs);

    //env->regs[0] = nms->kbootargs_pa;
    //cpu_set_pc(CPU(cpu), 0xc00607ec);
    cpu_set_pc(CPU(cpu), 0);
    //env->regs[0] = 0x9000000;
    //cpu_set_pc(CPU(cpu), LLB_BASE + 0x100);
    //cpu_set_pc(CPU(cpu), VROM_MEM_BASE);
}

/*
USB PHYS
*/
static uint64_t S5L8702_usb_phys_read(void *opaque, hwaddr addr, unsigned size)
{
    S5L8702_usb_phys_s *s = opaque;

    switch(addr)
    {
    case 0x0: // OPHYPWR
        return s->usb_ophypwr;

    case 0x4: // OPHYCLK
        return s->usb_ophyclk;

    case 0x8: // ORSTCON
        return s->usb_orstcon;

    case 0x20: // OPHYTUNE
        return s->usb_ophytune;

    case 0x28: // UCONDET
        return s->usb_ucondet;

    default:
        fprintf(stderr, "%s: read invalid location 0x%08x\n", __func__, addr);
        return 0;
    }

    return 0;
}

static void S5L8702_usb_phys_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    S5L8702_usb_phys_s *s = opaque;

    switch(addr)
    {
    case 0x0: // OPHYPWR
        s->usb_ophypwr = val;
        return;

    case 0x4: // OPHYCLK
        s->usb_ophyclk = val;
        return;

    case 0x8: // ORSTCON
        s->usb_orstcon = val;
        return;

    case 0x20: // OPHYTUNE
        s->usb_ophytune = val;
        return;

    case 0x28: // UCONDET
        s->usb_ucondet = val;
        return;

    default:
        //hw_error("%s: write invalid location 0x%08x.\n", __func__, offset);
        fprintf(stderr, "%s: write invalid location 0x%08x\n", __func__, addr);
    }
}

static const MemoryRegionOps usb_phys_ops = {
    .read = S5L8702_usb_phys_read,
    .write = S5L8702_usb_phys_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

/*
MBX
*/
static uint64_t S5L8702_mbx_read(void *opaque, hwaddr addr, unsigned size)
{
    //fprintf(stderr, "%s: read from location 0x%08x\n", __func__, addr);
    switch(addr)
    {
        case 0x12c:
            return 0x100;
        case 0xf00:
            return (1 << 0x18) | 0x10000; // seems to be some kind of identifier
        case 0x1020:
            return 0x10000;
        default:
            break;
    }
    return 0;
}

static void S5L8702_mbx_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    //fprintf(stderr, "%s: writing 0x%08x to 0x%08x\n", __func__, val, addr);
    // do nothing
}

static const MemoryRegionOps mbx_ops = {
    .read = S5L8702_mbx_read,
    .write = S5L8702_mbx_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void ipod_nano3g_memory_setup(MachineState *machine, MemoryRegion *sysmem, AddressSpace *nsas)
{
    IPodNano3GMachineState *nms = IPOD_NANO3G_MACHINE(machine);

    allocate_ram(sysmem, "sram1", SRAM1_MEM_BASE, 0x10000);

    // allocate UART ram
    allocate_ram(sysmem, "ram", RAM_MEM_BASE, 0x08000000);

    // load the bootrom
    uint8_t *file_data = NULL;
    unsigned long fsize;
    if (g_file_get_contents(nms->bootrom_path, (char **)&file_data, &fsize, NULL)) {
        // PATCHES
        // 1. do not verify the image header, just assume it's good to go (immediately return 1 from verify_img_header)
        ((uint32_t*)file_data)[0x58c/4] = 0xE3A00001;
        ((uint32_t*)file_data)[0x590/4] = 0xE12FFF1E;
        
        // 2. do the same for verify_decrypt_image (it's already decrypted off of NOR)
        ((uint32_t*)file_data)[0x6c0/4] = 0xE3A00001;
        ((uint32_t*)file_data)[0x6c4/4] = 0xE12FFF1E;

        allocate_ram(sysmem, "vrom", 0, 0x10000);
        address_space_rw(nsas, 0, MEMTXATTRS_UNSPECIFIED, (uint8_t *)file_data, fsize, 1);

        allocate_ram(sysmem, "vrom1", VROM_MEM_BASE, 0x10000);
        address_space_rw(nsas, VROM_MEM_BASE, MEMTXATTRS_UNSPECIFIED, (uint8_t *)file_data, fsize, 1);
    } else {
        printf("Failed to load bootrom.\n");
        exit(1);
    }

    allocate_ram(sysmem, "llb", LLB_BASE, align_64k_high(0x400000));

    allocate_ram(sysmem, "edgeic", EDGEIC_MEM_BASE, 0x1000);
    allocate_ram(sysmem, "watchdog", WATCHDOG_MEM_BASE, align_64k_high(0x1));

    allocate_ram(sysmem, "iis0", IIS0_MEM_BASE, align_64k_high(0x1));
    allocate_ram(sysmem, "iis1", IIS1_MEM_BASE, align_64k_high(0x1));
    allocate_ram(sysmem, "iis2", IIS2_MEM_BASE, align_64k_high(0x1));

    // allocate_ram(sysmem, "mpvd", MPVD_MEM_BASE, 0x70000);
    allocate_ram(sysmem, "h264bpd", H264BPD_MEM_BASE, 4096);

    allocate_ram(sysmem, "framebuffer", FRAMEBUFFER_MEM_BASE, align_64k_high(4 * 320 * 480));
    uint8_t stuff[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    address_space_rw(nsas, FRAMEBUFFER_MEM_BASE, MEMTXATTRS_UNSPECIFIED, (uint8_t *)stuff, 16, 1);
}

static char *ipod_nano3g_get_bootrom_path(Object *obj, Error **errp)
{
    IPodNano3GMachineState *nms = IPOD_NANO3G_MACHINE(obj);
    return g_strdup(nms->bootrom_path);
}

static void ipod_nano3g_set_bootrom_path(Object *obj, const char *value, Error **errp)
{
    IPodNano3GMachineState *nms = IPOD_NANO3G_MACHINE(obj);
    g_strlcpy(nms->bootrom_path, value, sizeof(nms->bootrom_path));
}

static char *ipod_nano3g_get_bootloader_path(Object *obj, Error **errp)
{
    IPodNano3GMachineState *nms = IPOD_NANO3G_MACHINE(obj);
    return g_strdup(nms->bootloader_path);
}

static void ipod_nano3g_set_bootloader_path(Object *obj, const char *value, Error **errp)
{
    IPodNano3GMachineState *nms = IPOD_NANO3G_MACHINE(obj);
    g_strlcpy(nms->bootloader_path, value, sizeof(nms->bootloader_path));
}

static void ipod_nano3g_instance_init(Object *obj)
{
	object_property_add_str(obj, "bootrom", ipod_nano3g_get_bootrom_path, ipod_nano3g_set_bootrom_path);
    object_property_set_description(obj, "bootrom", "Path to the S5L8730 bootrom binary");

    object_property_add_str(obj, "bootloader", ipod_nano3g_get_bootloader_path, ipod_nano3g_set_bootloader_path);
    object_property_set_description(obj, "bootloader", "Path to the decrypted EFI bootloader");
}

static inline qemu_irq S5L8702_get_irq(IPodNano3GMachineState *s, int n)
{
    return s->irq[n / S5L8702_VIC_SIZE][n % S5L8702_VIC_SIZE];
}

static uint32_t S5L8702_usb_hwcfg[] = {
    0,
    0x7a8f60d0,
    0x082000e8,
    0x01f08024
};

static void ipod_nano3g_key_event(void *opaque, int keycode)
{
    bool do_irq = false;
    int gpio_group = 0, gpio_selector = 0;

    IPodNano3GMultitouchState *s = (IPodNano3GMultitouchState *)opaque;
    if(keycode == 25 || keycode == 153) {
        // power button
        gpio_group = GPIO_BUTTON_POWER_IRQ / NUM_GPIO_PINS;
        gpio_selector = GPIO_BUTTON_POWER_IRQ % NUM_GPIO_PINS;
        
        if(keycode == 25 && (s->gpio_state->gpio_state & (1 << (GPIO_BUTTON_POWER & 0xf))) == 0) {
            s->gpio_state->gpio_state |= (1 << (GPIO_BUTTON_POWER & 0xf));
            do_irq = true;
        }
        else if(keycode == 153) {
            s->gpio_state->gpio_state &= ~(1 << (GPIO_BUTTON_POWER & 0xf));
            do_irq = true;
        }
    }
    else if(keycode == 35 || keycode == 163) {
        // home button
        gpio_group = GPIO_BUTTON_HOME_IRQ / NUM_GPIO_PINS;
        gpio_selector = GPIO_BUTTON_HOME_IRQ % NUM_GPIO_PINS;

        if(keycode == 35 && (s->gpio_state->gpio_state & (1 << (GPIO_BUTTON_HOME & 0xf))) == 0) {
            s->gpio_state->gpio_state |= (1 << (GPIO_BUTTON_HOME & 0xf));
            do_irq = true;
        }
        else if(keycode == 163) {
            s->gpio_state->gpio_state &= ~(1 << (GPIO_BUTTON_HOME & 0xf));
            do_irq = true;
        }
    }
    
    if(do_irq) {
        s->sysic->gpio_int_status[gpio_group] |= (1 << gpio_selector);
        qemu_irq_raise(s->sysic->gpio_irqs[gpio_group]);
    }
}

static void ipod_nano3g_machine_init(MachineState *machine)
{
	IPodNano3GMachineState *nms = IPOD_NANO3G_MACHINE(machine);
	MemoryRegion *sysmem;
    AddressSpace *nsas;
    ARMCPU *cpu;

    ipod_nano3g_cpu_setup(machine, &sysmem, &cpu, &nsas);

    // setup clock
    nms->sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(nms->sysclk, 12000000ULL);

    nms->cpu = cpu;

    // setup VICs
    nms->irq = g_malloc0(sizeof(qemu_irq *) * 2);
    DeviceState *dev = pl192_manual_init("vic0", qdev_get_gpio_in(DEVICE(nms->cpu), ARM_CPU_IRQ), qdev_get_gpio_in(DEVICE(nms->cpu), ARM_CPU_FIQ), NULL);
    PL192State *s = PL192(dev);
    nms->vic0 = s;
    memory_region_add_subregion(sysmem, VIC0_MEM_BASE, &nms->vic0->iomem);
    nms->irq[0] = g_malloc0(sizeof(qemu_irq) * 32);
    for (int i = 0; i < 32; i++) { nms->irq[0][i] = qdev_get_gpio_in(dev, i); }

    dev = pl192_manual_init("vic1", NULL);
    s = PL192(dev);
    nms->vic1 = s;
    memory_region_add_subregion(sysmem, VIC1_MEM_BASE, &nms->vic1->iomem);
    nms->irq[1] = g_malloc0(sizeof(qemu_irq) * 32);
    for (int i = 0; i < 32; i++) { nms->irq[1][i] = qdev_get_gpio_in(dev, i); }

    // // chain VICs together
    nms->vic1->daisy = nms->vic0;

    // init clock 0
    dev = qdev_new("ipodnano3g.clock");
    IPodNano3GClockState *clock0_state = IPOD_NANO3G_CLOCK(dev);
    nms->clock0 = clock0_state;
    memory_region_add_subregion(sysmem, CLOCK0_MEM_BASE, &clock0_state->iomem);

    // init clock 1
    dev = qdev_new("ipodnano3g.clock");
    IPodNano3GClockState *clock1_state = IPOD_NANO3G_CLOCK(dev);
    nms->clock1 = clock1_state;
    memory_region_add_subregion(sysmem, CLOCK1_MEM_BASE, &clock1_state->iomem);

    // init the timer
    dev = qdev_new("ipodnano3g.timer");
    IPodNano3GTimerState *timer_state = IPOD_NANO3G_TIMER(dev);
    nms->timer1 = timer_state;
    memory_region_add_subregion(sysmem, TIMER1_MEM_BASE, &timer_state->iomem);
    SysBusDevice *busdev = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(busdev, 0, S5L8702_get_irq(nms, S5L8702_TIMER1_IRQ));
    timer_state->sysclk = nms->sysclk;

    // init sysic
    dev = qdev_new("ipodnano3g.sysic");
    IPodNano3GSYSICState *sysic_state = IPOD_NANO3G_SYSIC(dev);
    nms->sysic = (IPodNano3GSYSICState *) g_malloc0(sizeof(struct IPodNano3GSYSICState));
    memory_region_add_subregion(sysmem, SYSIC_MEM_BASE, &sysic_state->iomem);
    busdev = SYS_BUS_DEVICE(dev);
    for(int grp = 0; grp < GPIO_NUMINTGROUPS; grp++) {
        sysbus_connect_irq(busdev, grp, S5L8702_get_irq(nms, S5L8702_GPIO_IRQS[grp]));
    }

    sysbus_connect_irq(busdev, 0, S5L8702_get_irq(nms, S5L8702_GPIO_G0_IRQ));
    sysbus_connect_irq(busdev, 0, S5L8702_get_irq(nms, S5L8702_GPIO_G1_IRQ));
    sysbus_connect_irq(busdev, 0, S5L8702_get_irq(nms, S5L8702_GPIO_G2_IRQ));
    sysbus_connect_irq(busdev, 0, S5L8702_get_irq(nms, S5L8702_GPIO_G3_IRQ));
    sysbus_connect_irq(busdev, 0, S5L8702_get_irq(nms, S5L8702_GPIO_G4_IRQ));
    sysbus_connect_irq(busdev, 0, S5L8702_get_irq(nms, S5L8702_GPIO_G5_IRQ));
    sysbus_connect_irq(busdev, 0, S5L8702_get_irq(nms, S5L8702_GPIO_G6_IRQ));

    // init GPIO
    dev = qdev_new("ipodnano3g.gpio");
    IPodNano3GGPIOState *gpio_state = IPOD_NANO3G_GPIO(dev);
    nms->gpio_state = gpio_state;
    memory_region_add_subregion(sysmem, GPIO_MEM_BASE, &gpio_state->iomem);

    // init SDIO
    dev = qdev_new("ipodnano3g.sdio");
    IPodNano3GSDIOState *sdio_state = IPOD_NANO3G_SDIO(dev);
    nms->sdio_state = sdio_state;
    memory_region_add_subregion(sysmem, SDIO_MEM_BASE, &sdio_state->iomem);

    dev = exynos4210_uart_create(UART0_MEM_BASE, 256, 0, serial_hd(0), nms->irq[0][24]);
    if (!dev) {
        printf("Failed to create uart0 device!\n");
        abort();
    }

    dev = exynos4210_uart_create(UART1_MEM_BASE, 256, 1, serial_hd(1), nms->irq[0][25]);
    if (!dev) {
        printf("Failed to create uart1 device!\n");
        abort();
    }

    dev = exynos4210_uart_create(UART2_MEM_BASE, 256, 2, serial_hd(2), nms->irq[0][26]);
    if (!dev) {
        printf("Failed to create uart2 device!\n");
        abort();
    }

    dev = exynos4210_uart_create(UART3_MEM_BASE, 256, 3, serial_hd(3), nms->irq[0][27]);
    if (!dev) {
        printf("Failed to create uart3 device!\n");
        abort();
    }

    dev = exynos4210_uart_create(UART4_MEM_BASE, 256, 4, serial_hd(4), nms->irq[0][28]);
    if (!dev) {
        printf("Failed to create uart4 device!\n");
        abort();
    }

    // init spis
    set_spi_base(0);
    dev = sysbus_create_simple("S5L8702spi", SPI0_MEM_BASE, S5L8702_get_irq(nms, S5L8702_SPI0_IRQ));
    S5L8702SPIState *spi0_state = S5L8702SPI(dev);
    spi0_state->nor->bootloader_path = nms->bootloader_path;

    set_spi_base(1);
    sysbus_create_simple("S5L8702spi", SPI1_MEM_BASE, S5L8702_get_irq(nms, S5L8702_SPI1_IRQ));

    set_spi_base(2);
    dev = sysbus_create_simple("S5L8702spi", SPI2_MEM_BASE, S5L8702_get_irq(nms, S5L8702_SPI2_IRQ));
    S5L8702SPIState *spi2_state = S5L8702SPI(dev);
    spi2_state->mt->sysic = sysic_state;
    spi2_state->mt->gpio_state = gpio_state;
    nms->spi2_state = spi2_state;

    ipod_nano3g_memory_setup(machine, sysmem, nsas);

    // init LCD
    dev = qdev_new("ipodnano3g.lcd");
    IPodNano3GLCDState *lcd_state = IPOD_NANO3G_LCD(dev);
    lcd_state->sysmem = sysmem;
    lcd_state->nsas = nsas;
    lcd_state->mt = spi2_state->mt;
    nms->lcd_state = lcd_state;
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(busdev, 0, S5L8702_get_irq(nms, S5L8702_LCD_IRQ));
    memory_region_add_subregion(sysmem, DISPLAY_MEM_BASE, &lcd_state->iomem);
    sysbus_realize(busdev, &error_fatal);

    // init AES engine
    dev = qdev_new("ipodnano3g.aes");
    S5L8702AESState *aes_state = IPOD_NANO3G_AES(dev);
    nms->aes_state = aes_state;
    memory_region_add_subregion(sysmem, AES_MEM_BASE, &aes_state->iomem);

    // init SHA1 engine
    dev = qdev_new("ipodnano3g.sha1");
    S5L8702SHA1State *sha1_state = IPOD_NANO3G_SHA1(dev);
    nms->sha1_state = sha1_state;
    memory_region_add_subregion(sysmem, SHA1_MEM_BASE, &sha1_state->iomem);

    // init 8702 engine
    MemoryRegion *iomem = g_new(MemoryRegion, 1);
    memory_region_init_io(iomem, OBJECT(s), &engine_8702_ops, nsas, "8702engine", 0x100);
    memory_region_add_subregion(sysmem, ENGINE_8702_MEM_BASE, iomem);

    // init NAND flash
    dev = qdev_new("itnand");
    ITNandState *nand_state = ITNAND(dev);
    nand_state->nand_path = "nope";
    nand_state->downstream_as = nsas;
    nms->nand_state = nand_state;
    //object_property_set_link(OBJECT(dev), "downstream", OBJECT(sysmem), &error_fatal);
    memory_region_add_subregion(sysmem, NAND_MEM_BASE, &nand_state->iomem);

    // init NAND ECC module
    dev = qdev_new("itnand_ecc");
    ITNandECCState *nand_ecc_state = ITNANDECC(dev);
    nms->nand_ecc_state = nand_ecc_state;
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(busdev, 0, S5L8702_get_irq(nms, S5L8702_NAND_ECC_IRQ));
    memory_region_add_subregion(sysmem, NAND_ECC_MEM_BASE, &nand_ecc_state->iomem);

    // init USB OTG
    dev = ipod_nano3g_init_usb_otg(nms->irq[0][13], S5L8702_usb_hwcfg);
    synopsys_usb_state *usb_otg = S5L8702USBOTG(dev);
    nms->usb_otg = usb_otg;
    memory_region_add_subregion(sysmem, USBOTG_MEM_BASE, &nms->usb_otg->iomem);

    // init USB PHYS
    S5L8702_usb_phys_s *usb_state = malloc(sizeof(S5L8702_usb_phys_s));
    nms->usb_phys = usb_state;
    usb_state->usb_ophypwr = 0;
    usb_state->usb_ophyclk = 0;
    usb_state->usb_orstcon = 0;
    usb_state->usb_ophytune = 0;
    usb_state->usb_ucondet = 1;

    iomem = g_new(MemoryRegion, 1);
    memory_region_init_io(iomem, OBJECT(s), &usb_phys_ops, usb_state, "usbphys", 0x40);
    memory_region_add_subregion(sysmem, USBPHYS_MEM_BASE, iomem);

    // TODO: unknown peripheral at 0x38500000
    allocate_ram(sysmem, "ipod.unknown", 0x38500000, 0x2000);

    // various bus control peripherals that the bootloader whishes to poke
    allocate_ram(sysmem, "ipod.ahb", 0x38100000, 0x1000);
    allocate_ram(sysmem, "ipod.miu", 0x3e000000, 0x1000);
    allocate_ram(sysmem, "ipod.axi", 0x3d500000, 0x1000);
    // pl301 is even worse, the bootloader reads from it
    iomem = g_new(MemoryRegion, 1);
    memory_region_init_io(iomem, OBJECT(nms), &pl301_workaround_ops, NULL, "pl301", 0x1000);
    memory_region_add_subregion(sysmem, 0x3ff00000, iomem);
    // prng is similarly annoying
    iomem = g_new(MemoryRegion, 1);
    memory_region_init_io(iomem, OBJECT(nms), &prng_workaround_ops, NULL, "prng", 0x1000);
    memory_region_add_subregion(sysmem, 0x3c100000, iomem);


    // contains some constants
    // data = malloc(4);
    // data[0] = ENGINE_8702_MEM_BASE; // engine base address
    // address_space_rw(nsas, LLB_BASE + 0x208, MEMTXATTRS_UNSPECIFIED, (uint8_t *)data, 4, 1);

    // init two pl080 DMAC0 devices
    dev = qdev_new("pl080");
    PL080State *pl080_1 = PL080(dev);
    object_property_set_link(OBJECT(dev), "downstream", OBJECT(sysmem), &error_fatal);
    memory_region_add_subregion(sysmem, DMAC0_MEM_BASE, &pl080_1->iomem);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize(busdev, &error_fatal);
    sysbus_connect_irq(busdev, 0, S5L8702_get_irq(nms, S5L8702_DMAC0_IRQ));

    dev = qdev_new("pl080");
    PL080State *pl080_2 = PL080(dev);
    object_property_set_link(OBJECT(dev), "downstream", OBJECT(sysmem), &error_fatal);
    memory_region_add_subregion(sysmem, DMAC1_MEM_BASE, &pl080_2->iomem);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize(busdev, &error_fatal);
    sysbus_connect_irq(busdev, 0, S5L8702_get_irq(nms, S5L8702_DMAC1_IRQ));

    // Init I2C
    dev = qdev_new("ipodnano3g.i2c");
    IPodNano3GI2CState *i2c_state = IPOD_NANO3G_I2C(dev);
    nms->i2c0_state = i2c_state;
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(busdev, 0, S5L8702_get_irq(nms, S5L8702_I2C0_IRQ));
    memory_region_add_subregion(sysmem, I2C0_MEM_BASE, &i2c_state->iomem);

    // init the PMU
    I2CSlave *pmu = i2c_slave_create_simple(i2c_state->bus, "pcf50633", 0xe6);

    dev = qdev_new("ipodnano3g.i2c");
    i2c_state = IPOD_NANO3G_I2C(dev);
    nms->i2c1_state = i2c_state;
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(busdev, 0, S5L8702_get_irq(nms, S5L8702_I2C1_IRQ));
    memory_region_add_subregion(sysmem, I2C1_MEM_BASE, &i2c_state->iomem);

    // init the ADM
    dev = qdev_new("ipodnano3g.adm");
    IPodNano3GADMState *adm_state = IPOD_NANO3G_ADM(dev);
    adm_state->nand_state = nand_state;
    nms->adm_state = adm_state;
    object_property_set_link(OBJECT(dev), "downstream", OBJECT(sysmem), &error_fatal);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize(busdev, &error_fatal);
    sysbus_connect_irq(busdev, 0, S5L8702_get_irq(nms, S5L8702_ADM_IRQ));
    memory_region_add_subregion(sysmem, ADM_MEM_BASE, &adm_state->iomem);

    // init JPEG engine
    dev = qdev_new("s5l8702jpeg");
    S5L8702JPEGState *jpeg_state = S5L8702JPEG(dev);
    nms->jpeg_state = jpeg_state;
    jpeg_state->sysmem = sysmem;
    jpeg_state->nsas = nsas;
    // object_property_set_link(OBJECT(dev), "downstream", OBJECT(sysmem), &error_fatal);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_realize(busdev, &error_fatal);
    sysbus_connect_irq(busdev, 0, S5L8702_get_irq(nms, S5L8702_ADM_IRQ));
    memory_region_add_subregion(sysmem, MPVD_MEM_BASE, &jpeg_state->iomem);

    // Init DRAM Express (DREX) controller
    dev = qdev_new("ipodnano5g.drex");
    IPodNano5GDREXState *drex_state = IPOD_NANO5G_DREX(dev);
    nms->drex_state = drex_state;
    memory_region_add_subregion(sysmem, DREX_MEM_BASE, &drex_state->iomem);

    // init MBX
    // iomem = g_new(MemoryRegion, 1);
    // memory_region_init_io(iomem, OBJECT(nms), &mbx_ops, NULL, "mbx", 0x1000000);
    // memory_region_add_subregion(sysmem, MBX_MEM_BASE, iomem);

    // init the chip ID module
    dev = qdev_new("ipodnano3g.chipid");
    IPodNano3GChipIDState *chipid_state = IPOD_NANO3G_CHIPID(dev);
    nms->chipid_state = chipid_state;
    memory_region_add_subregion(sysmem, CHIPID_MEM_BASE, &chipid_state->iomem);

    // init the TVOut instances
    dev = qdev_new("ipodnano3g.tvout");
    IPodNano3GTVOutState *tvout_state = IPOD_NANO3G_TVOUT(dev);
    tvout_state->index = 1;
    nms->tvout1_state = tvout_state;
    memory_region_add_subregion(sysmem, TVOUT1_MEM_BASE, &tvout_state->iomem);

    dev = qdev_new("ipodnano3g.tvout");
    tvout_state = IPOD_NANO3G_TVOUT(dev);
    tvout_state->index = 2;
    nms->tvout2_state = tvout_state;
    memory_region_add_subregion(sysmem, TVOUT2_MEM_BASE, &tvout_state->iomem);
    busdev = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(busdev, 0, S5L8702_get_irq(nms, S5L8702_TVOUT_SDO_IRQ));

    dev = qdev_new("ipodnano3g.tvout");
    tvout_state = IPOD_NANO3G_TVOUT(dev);
    tvout_state->index = 3;
    nms->tvout3_state = tvout_state;
    memory_region_add_subregion(sysmem, TVOUT3_MEM_BASE, &tvout_state->iomem);

    // setup workaround for TVOut
    iomem = g_new(MemoryRegion, 1);
    memory_region_init_io(iomem, OBJECT(nms), &tvout_workaround_ops, NULL, "tvoutworkaround", 0x4);
    memory_region_add_subregion(sysmem, TVOUT_WORKAROUND_MEM_BASE, iomem);

    qemu_register_reset(ipod_nano3g_cpu_reset, nms);

    qemu_add_kbd_event_handler(ipod_nano3g_key_event, spi2_state->mt);
}

static void ipod_nano3g_machine_class_init(ObjectClass *obj, void *data)
{
    MachineClass *mc = MACHINE_CLASS(obj);
    mc->desc = "iPod Touch";
    mc->init = ipod_nano3g_machine_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm1176");
}

static const TypeInfo ipod_nano3g_machine_info = {
    .name          = TYPE_IPOD_NANO3G_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(IPodNano3GMachineState),
    .class_size    = sizeof(IPodNano3GMachineClass),
    .class_init    = ipod_nano3g_machine_class_init,
    .instance_init = ipod_nano3g_instance_init,
};

static void ipod_nano3g_machine_types(void)
{
    type_register_static(&ipod_nano3g_machine_info);
}

type_init(ipod_nano3g_machine_types)

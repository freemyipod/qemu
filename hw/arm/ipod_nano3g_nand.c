#include "hw/arm/ipod_nano3g_nand.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"

static uint64_t itnand_read(void *opaque, hwaddr addr, unsigned size);
static void itnand_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

static void fmiss_vm_reset(fmiss_vm *vm, uint32_t pc) {
    for (int i = 0; i < 8; i++) {
        vm->regs[i] = 0;
    }
    vm->start_pc = pc;
    vm->pc = pc;
}

static uint64_t fmiss_vm_fetch(fmiss_vm *vm) {
    uint64_t val = 0;
    address_space_read(vm->iomem, vm->pc, MEMTXATTRS_UNSPECIFIED, &val, 8);
    return val;
}

// Reference: https://github.com/lemonjesus/S5L8702-FMISS-Tools/blob/main/Documentation.md
static bool fmiss_vm_step(void *opaque, fmiss_vm *vm) {
    uint64_t ins = fmiss_vm_fetch(vm);

    uint32_t imm = ins >> 32;
    uint8_t opcode = ins >> 24;
    uint8_t dst = ins >> 16;
    uint16_t src = ins;

    uint32_t addr, data;


    printf("fmiss_vm: at %08x: %016lx (%02x %02x %04x %08x)",
        vm->pc - vm->start_pc, ins, opcode, dst, src, imm);
    for (int i = 0; i < 8; i++) {
        printf(" %d:%08lx", i, vm->regs[i]);
    }
    printf("\n");
    switch (opcode) {
    case 0: // Terminate.
        return false;
    case 1: // Write immediate to memory.
        itnand_write(opaque, src, imm, 4);
        break;
    case 2: // Write register to memory.
        itnand_write(opaque, src, vm->regs[dst%8], 4);
        break;
    case 3: // Read memory to register.
        addr = vm->regs[src%8];
        data = 0;
        address_space_read(vm->iomem, addr, MEMTXATTRS_UNSPECIFIED, &data, 4);
        printf("fmiss_vm: mem %08x -> %08x\n", addr, data);
        vm->regs[dst%8] = data;
        break;
    case 4: // Read from memory into a register.
        uint32_t val = itnand_read(opaque, src, 4);
        vm->regs[dst%8] = val;
        break;
    case 5: // Read immediate into register.
        vm->regs[dst%8] = imm;
        break;
    case 6: // Transfer register to register.
        vm->regs[dst%8] = vm->regs[src%8];
    case 7: // Unknown, possibly wait for FMCSTAT. No-op.
        break;
    case 10: // AND two registers and an immediate.
        if (imm == 0) {
            vm->regs[dst%8] &= vm->regs[src%8];
        } else {
            vm->regs[dst%8] = vm->regs[src%8] & imm;
        }
        break;
    case 11: // OR two registers and an immediate.
        if (imm == 0) {
            vm->regs[dst%8] |= vm->regs[src%8];
        } else {
            vm->regs[dst%8] = vm->regs[src%8] | imm;
        }
        break;
    case 12: // Add an Immediate to a Register
        if (imm == 0) {
            vm->regs[dst%8] += vm->regs[src%8];
        } else {
            vm->regs[dst%8] = vm->regs[src%8] + imm;
        }
        break;
    case 13: // Subtract an Immediate from a Register
        if (imm == 0) {
            vm->regs[dst%8] -= vm->regs[src%8];
        } else {
            vm->regs[dst%8] = vm->regs[src%8] - imm;
        }
        break;
    case 14: // Jump If Not Equal.
        if (vm->regs[dst%8] != src) {
            vm->pc = vm->start_pc + imm;
            return true;
        }
        break;
    case 17: // Store a Register Value to a Memory Location Pointed to by a Register.
        addr = vm->regs[src%8];
        data = vm->regs[dst%8];
        printf("fmiss_vm: mem %08x <- %08x\n", addr, data);
        address_space_write(vm->iomem, addr, MEMTXATTRS_UNSPECIFIED, &data, 4);
        break;
    case 19: // Left Shift a Register by an Immediate
        if (imm == 0) {
            vm->regs[dst%8] <<= vm->regs[src%8];
        } else {
            vm->regs[dst%8] = vm->regs[src%8] << imm;
        }
        break;
    case 20: // Right Shift a Register by an Immediate
        if (imm == 0) {
            vm->regs[dst%8] >>= vm->regs[src%8];
        } else {
            vm->regs[dst%8] = vm->regs[src%8] >> imm;
        }
        break;
    case 23: // Jump if equal, but apparently not?
        if (vm->regs[dst%8] == src) {
            vm->pc = vm->start_pc + imm;
            return true;
        }
        break;
    default:
        printf("fmiss_vm: unimplemented opcode %d!\n", opcode);
        return false;
    }
    vm->pc += 8;
    return true;
}

static void fmiss_vm_execute(void *opaque, fmiss_vm *vm) {
    printf("fmiss_vm: starting execution at %08lx...\n", vm->start_pc);
    while (fmiss_vm_step(opaque, vm)) {
    }
    printf("fmiss_vm: done executing\n");
}

static int get_bank(ITNandState *s) {
    uint32_t bank_bitmap = (s->fmctrl0 >> 1) & 0xFF;
    for(int bank = 0; bank < NAND_NUM_BANKS; bank++) {
        if((bank_bitmap & (1 << bank)) != 0) {
            return bank;
        }
    }
    return -1;
}

static void set_bank(ITNandState *s, uint32_t activate_bank) {
    for(int bank = 0; bank < 8; bank++) {
        // clear bit, toggle if it is active
        s->fmctrl0 &= ~(1 << (bank + 1));
        if(bank == activate_bank) {
            s->fmctrl0 ^= 1 << (bank + 1);
        }
    }
}

void nand_set_buffered_page(ITNandState *s, uint32_t page) {
    uint32_t bank = get_bank(s);
    if(bank == -1) {
        hw_error("Active bank not set while nand_read with page %d is called (reading multiple pages: %d)!", page, s->reading_multiple_pages);
    }

    if(bank != s->buffered_bank || page != s->buffered_page) {
        // refresh the buffered page
        //uint32_t vpn = page * 8 + bank;
        char filename[200];
        sprintf(filename, "%s/bank%d/%d.page", s->nand_path, bank, page);
        struct stat st = {0};
        if (stat(filename, &st) == -1) {
            // page storage does not exist - initialize an empty buffer
            memset(s->page_buffer, 0, NAND_BYTES_PER_PAGE);
            memset(s->page_spare_buffer, 0, NAND_BYTES_PER_SPARE);
            s->page_spare_buffer[0xA] = 0xFF; // make sure we add the FTL mark to an empty page
        }
        else {
            FILE *f = fopen(filename, "rb");
            if (f == NULL) { hw_error("Unable to read file!"); }
            fread(s->page_buffer, sizeof(char), NAND_BYTES_PER_PAGE, f);
            fread(s->page_spare_buffer, sizeof(char), NAND_BYTES_PER_SPARE, f);
            fclose(f);
        }

        s->buffered_page = page;
        s->buffered_bank = bank;
        // printf("Buffered bank: %d, page: %d\n", s->buffered_bank, s->buffered_page);
    }
}

static uint32_t itnand_fifo_read(void *opaque)
{
    ITNandState *s = (ITNandState *) opaque;
    switch (s->cmd) {
    case NAND_CMD_ID:
        uint8_t bank = (s->fmctrl0 >> 1) & 0xff;
        switch (bank) {
        case 1:
        case 2:
            return NAND_CHIP_ID;
        default:
            return 0;
        }
    case NAND_CMD_READSTATUS:
        return (1 << 6);
    case NAND_CMD_READ_PAGE:
        uint32_t read_val = 0;
        if(s->reading_multiple_pages) {
            // which bank are we at?
            if(s->fmdnum % 0x800 == 0) {
                s->cur_bank_reading += 1;
                //printf("WILL TURN TO BANK %d (cnt: %d)\n", s->cur_bank_reading, s->fmdnum);
                set_bank(s, s->banks_to_read[s->cur_bank_reading]);
            }

            // compute the offset in the page
            uint32_t page_offset = s->fmdnum % 0x800;
            if(page_offset == 0) { page_offset = 0x800; }
            nand_set_buffered_page(s, s->pages_to_read[s->cur_bank_reading]);
            //printf("Reading page %d\n", s->pages_to_read[s->cur_bank_reading]);
            read_val = ((uint32_t *)s->page_buffer)[(NAND_BYTES_PER_PAGE - page_offset) / 4];
            //printf("FMDNUM: %d, offset: %d\n", s->fmdnum, (NAND_BYTES_PER_PAGE - page_offset) / 4);
            //printf("Page offset: %d, bytes: 0x%08x\n", page_offset, read_val);
        }
        else {
            uint32_t page = (s->fmaddr1 << 16) | (s->fmaddr0 >> 16);
            nand_set_buffered_page(s, page);

            if(s->reading_spare) {
                read_val = ((uint32_t *)s->page_spare_buffer)[(NAND_BYTES_PER_SPARE - s->fmdnum - 1) / 4];
                printf("fmc: Reading spare page %d -> %08x\n", page, read_val);
            } else {
                read_val = ((uint32_t *)s->page_buffer)[(NAND_BYTES_PER_PAGE - s->fmdnum - 1) / 4];
                printf("fmc: Reading page %d -> %08x\n", page, read_val);
            }
        }
        s->fmdnum -= 4;
        return read_val;
    default:
        printf("fmc: unhandled FIFO read with CMD: %08lx\n", s->cmd);
        return 0xdeadbeef;
    }
}

static uint64_t itnand_read(void *opaque, hwaddr addr, unsigned size)
{
    ITNandState *s = (ITNandState *) opaque;
    //printf("%s(%08lx)...\n", __func__, addr);
    if(s->reading_multiple_pages) {
        fprintf(stderr, "%s: reading from 0x%08lx\n", __func__, addr);
    }

    if (addr >= FMI_DMEM && addr < (FMI_DMEM+4*FMIVSS_DMEM_SIZE)) {
        uint32_t i = (addr - FMI_DMEM)/4;
        return s->fmiss_vm.dmem[i];
    }

    if (addr >= NAND_MEMFIFO && addr <(NAND_MEMFIFO+NAND_MEMFIFO_SIZE*8)) {
        uint32_t i = (addr - NAND_MEMFIFO)/4;
        return s->memfifo[i];
    }

    switch (addr) {
        case NAND_FMCTRL0:
            return s->fmctrl0;
        case NAND_FMCTRL1:
            return s->fmctrl1;
        case NAND_FMFIFO:
            return itnand_fifo_read(opaque);
        case NAND_FMCSTAT:
            return (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 12); // this indicates that everything is ready, including our eight banks
        case NAND_RSCTRL:
            return s->rsctrl;
        case FMI_PROGRAM:
            return s->fmi_program;
        case FMI_INT:
            return s->fmi_int;
        case FMI_START:
            return 0;
        case 0xc64:
            return 1;
        default:
            break;
    }
    printf("fmc: unhandled MMIO read %08lx\n", addr);
    return 0;
}

static void itnand_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    //printf("%s(%08lx, %08lx)...\n", __func__, addr, val);
    ITNandState *s = (ITNandState *) opaque;
    if(s->reading_multiple_pages) {
        fprintf(stderr, "%s: writing 0x%08lx to 0x%08lx\n", __func__, val, addr);
    }

    if (addr >= FMI_DMEM && addr < (FMI_DMEM+4*FMIVSS_DMEM_SIZE)) {
        uint32_t i = (addr - FMI_DMEM)/4;
        s->fmiss_vm.dmem[i] = val;
        return;
    }
    

    switch(addr) {
        case NAND_FMCTRL0:
            s->fmctrl0 = val;
            break;
        case NAND_FMCTRL1:
            s->fmctrl1 = val | (1<<30);
            switch (val&0b111) {
            case 0b001:
                printf("fmc: fmctrl1: %08lx, addr tx\n", val);
                break;
            case 0b010:
                printf("fmc: fmctrl1: %08lx, read tx\n", val);
                break;
            case 0b100:
                printf("fmc: fmctrl1: %08lx, write tx\n", val);
                break;
            }
            break;
        case NAND_FMADDR0:
            printf("fmc: fmaddr0: %08lx\n", val);
            s->fmaddr0 = val;
            break;
        case NAND_FMADDR1:
            printf("fmc: fmaddr1: %08lx\n", val);
            s->fmaddr1 = val;
            break;
        case NAND_FMANUM:
            printf("fmc: fmanum: %08lx\n", val);
            s->fmanum = val;
            break;
        case NAND_CMD:
            printf("fmc: cmd: %02x\n", val);
            s->cmd = val;
            break;
        case NAND_DMADEST:
            printf("fmc: dmadest: %02x\n", val);
            break;
        case NAND_FMDNUM:
            printf("fmc: fmdnum: %d\n", val);
            if(val == NAND_BYTES_PER_SPARE - 1) {
                s->reading_spare = 1;
            } else {
                s->reading_spare = 0;
            }
            s->fmdnum = val;
            break;
        case NAND_FMFIFO:
            if(!s->is_writing) {
                // printf("%s: NAND_FMFIFO writing while not in writing mode!\n", __func__);
                return;
            }

            //printf("Setting offset %d: %d\n", s->fmdnum, (NAND_BYTES_PER_PAGE - s->fmdnum) / 4);
            ((uint32_t *)s->page_buffer)[(NAND_BYTES_PER_PAGE - s->fmdnum) / 4] = val;
            s->fmdnum -= 4;

            if(s->fmdnum == 0) {
                // we're done!
                s->is_writing = false;

                // flush the page buffer to the disk
                //uint32_t vpn = s->buffered_page * 8 + s->buffered_bank;
                //printf("Flushing page %d, bank %d, vpn %d\n", s->buffered_page, s->buffered_bank, vpn);
                qemu_mutex_lock(&s->lock);
                qemu_mutex_unlock(&s->lock);
                {
                    char filename[200];
                    sprintf(filename, "%s/bank%d/%d_new.page", s->nand_path, s->buffered_bank, s->buffered_page);
                    FILE *f = fopen(filename, "wb");
                    if (f == NULL) { hw_error("Unable to read file!"); }
                    fwrite(s->page_buffer, sizeof(char), NAND_BYTES_PER_PAGE, f);
                    fwrite(s->page_spare_buffer, sizeof(char), NAND_BYTES_PER_SPARE, f);
                    fclose(f);
                }
            }
            break;
        case NAND_RSCTRL:
            s->rsctrl = val;
            break;
        case FMI_PROGRAM:
            s->fmi_program = val;
            break;
        case FMI_INT:
            s->fmi_int &= ~val;
            break;
        case FMI_START:
            if ((val & 1) == 0) {
                s->fmiss_vm.iomem = s->downstream_as;
                fmiss_vm_reset(&s->fmiss_vm, s->fmi_program);
                fmiss_vm_execute(opaque, &s->fmiss_vm);
                s->fmi_int |= 1;
            }
            break;
        case  NAND_MEMFIFO_STAT:
            if ((val & 2) == 2) {
                uint32_t count = (val >> 6);
                if (count > NAND_MEMFIFO_SIZE) {
                    count = NAND_MEMFIFO_SIZE;
                }
                for (int i = 0; i < count; i++) {
                    s->memfifo[i] = itnand_fifo_read(opaque);
                    printf("fmc: memfifo read %d -> %08lx\n", i, s->memfifo[i]);
                }
            }
            break;
        default:
            break;
    }
}

static const MemoryRegionOps nand_ops = {
    .read = itnand_read,
    .write = itnand_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void itnand_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    ITNandState *s = ITNAND(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &nand_ops, s, "nand", 0x1000);
    sysbus_init_irq(sbd, &s->irq);

    s->page_buffer = (uint8_t *)malloc(NAND_BYTES_PER_PAGE);
    s->page_spare_buffer = (uint8_t *)malloc(NAND_BYTES_PER_SPARE);
    s->buffered_page = -1;
    s->buffered_bank = -1;

    fmiss_vm_reset(&s->fmiss_vm, 0);

    qemu_mutex_init(&s->lock);
}

static void itnand_reset(DeviceState *d)
{
    ITNandState *s = (ITNandState *) d;

    s->fmctrl0 = 0;
    s->fmctrl1 = 0;
    s->fmaddr0 = 0;
    s->fmaddr1 = 0;
    s->fmanum = 0;
    s->fmdnum = 0;
    s->rsctrl = 0;
    s->cmd = 0;
    s->fmi_program = 0;
    s->fmi_int = 0;
    s->reading_spare = 0;
    s->buffered_page = -1;
    for (int i = 0; i < NAND_MEMFIFO_SIZE; i++) {
        s->memfifo[i] = 0;
    }
}

static void itnand_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->reset = itnand_reset;
}

static const TypeInfo itnand_info = {
    .name          = TYPE_ITNAND,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ITNandState),
    .instance_init = itnand_init,
    .class_init    = itnand_class_init,
};

static void itnand_register_types(void)
{
    type_register_static(&itnand_info);
}

type_init(itnand_register_types)

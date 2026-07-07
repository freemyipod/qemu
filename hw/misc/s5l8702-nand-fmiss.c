/*
 * S5L8702 NAND FMISS micro-VM (experimental).
 * Confirmed working for reading IDs and pages; writing/erasing unconfirmed.
 */

#include "qemu/osdep.h"
#include "hw/misc/s5l8702-nand-fmiss.h"
#include "trace.h"
#include "hw/hw.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"

void fmiss_vm_reset(fmiss_vm *vm, uint32_t pc) {
    for (int i = 0; i < 8; i++) {
        vm->regs[i] = 0;
    }
    vm->start_pc = pc;
    vm->pc = pc;
}

static uint64_t fmiss_vm_fetch(fmiss_vm *vm) {
    uint64_t val = 0;
    address_space_read(&address_space_memory, vm->pc, MEMTXATTRS_UNSPECIFIED, &val, 8);
    return val;
}

static bool fmiss_vm_step(const FmissNandOps *ops, void *opaque, fmiss_vm *vm) {
    uint64_t ins = fmiss_vm_fetch(vm);

    uint32_t imm     = ins >> 32;
    uint8_t  opcode  = ins >> 24;
    uint8_t  dst     = ins >> 16;
    uint16_t src     = ins;

    uint8_t dst_reg = dst % 8;
    uint8_t src_reg = src % 8;

    trace_s5l8702_fmiss_insn(vm->pc - vm->start_pc, opcode, dst, src, imm);

    switch (opcode) {
    case 0x00: /* Terminate */
        trace_s5l8702_fmiss_terminate();
        return false;

    case 0x01: /* Write immediate to NAND-register space */
        trace_s5l8702_fmiss_write_nand_imm((uint32_t)src, imm);
        ops->write(opaque, src, imm, 4);
        break;

    case 0x02: /* Write register to NAND-register space */
        trace_s5l8702_fmiss_write_nand_reg((uint32_t)src, vm->regs[dst_reg], dst_reg);
        ops->write(opaque, src, vm->regs[dst_reg], 4);
        break;

    case 0x03: /* Dereference address in register */
        address_space_read(&address_space_memory, vm->regs[src_reg] ^ 0x80000000, MEMTXATTRS_UNSPECIFIED, &vm->regs[dst_reg], 4);
        trace_s5l8702_fmiss_deref(dst_reg, src_reg, vm->regs[src_reg] ^ 0x80000000);
        break;

    case 0x04: /* Read from NAND-register space into a register */
        uint32_t val = ops->read(opaque, src, 4);
        vm->regs[dst_reg] = val & imm;
        trace_s5l8702_fmiss_read_nand(dst_reg, (uint32_t)src, val, imm);
        break;

    case 0x05: /* Load immediate into register */
        vm->regs[dst_reg] = imm;
        trace_s5l8702_fmiss_load_imm(dst_reg, imm);
        break;

    case 0x06: /* Move register to register */
        vm->regs[dst_reg] = vm->regs[src_reg];
        trace_s5l8702_fmiss_mov(dst_reg, src_reg, vm->regs[dst_reg]);
        break;

    case 0x07: /* Wait for FMCSTAT (no-op in emulation) */
        trace_s5l8702_fmiss_wait(dst);
        break;

    case 0x0a: /* AND */
        vm->regs[dst_reg] = imm == 0 ? vm->regs[dst_reg] & vm->regs[src_reg] : vm->regs[src_reg] & imm;
        trace_s5l8702_fmiss_alu(dst_reg, "and", src_reg, imm, vm->regs[dst_reg]);
        break;

    case 0x0b: /* OR */
        vm->regs[dst_reg] = imm == 0 ? vm->regs[dst_reg] | vm->regs[src_reg] : vm->regs[src_reg] | imm;
        trace_s5l8702_fmiss_alu(dst_reg, "or", src_reg, imm, vm->regs[dst_reg]);
        break;

    case 0x0c: /* ADD */
        vm->regs[dst_reg] = imm == 0 ? vm->regs[dst_reg] + vm->regs[src_reg] : vm->regs[src_reg] + imm;
        trace_s5l8702_fmiss_alu(dst_reg, "add", src_reg, imm, vm->regs[dst_reg]);
        break;

    case 0x0d: /* SUB */
        vm->regs[dst_reg] = imm == 0 ? vm->regs[dst_reg] - vm->regs[src_reg] : vm->regs[src_reg] - imm;
        trace_s5l8702_fmiss_alu(dst_reg, "sub", src_reg, imm, vm->regs[dst_reg]);
        break;

    case 0x0e: /* JNZ */
        trace_s5l8702_fmiss_branch("jnz", dst_reg, vm->regs[dst_reg], imm);
        if (vm->regs[dst_reg] != 0) {
            vm->pc = vm->start_pc + imm;
            return true;
        }
        break;

    case 0x11: /* Store register to address given by another register */
        uint32_t addr = vm->regs[src_reg] & ~0x80000000u;
        uint32_t data = vm->regs[dst_reg];
        trace_s5l8702_fmiss_store_mem(src_reg, addr, dst_reg, data);
        address_space_write(&address_space_memory, addr, MEMTXATTRS_UNSPECIFIED, &data, 4);
        break;

    case 0x13: /* SHL */
        vm->regs[dst_reg] = imm == 0 ? vm->regs[dst_reg] << vm->regs[src_reg] : vm->regs[src_reg] << imm;
        trace_s5l8702_fmiss_alu(dst_reg, "shl", src_reg, imm, vm->regs[dst_reg]);
        break;

    case 0x14: /* SHR */
        vm->regs[dst_reg] = imm == 0 ? vm->regs[dst_reg] >> vm->regs[src_reg] : vm->regs[src_reg] >> imm;
        trace_s5l8702_fmiss_alu(dst_reg, "shr", src_reg, imm, vm->regs[dst_reg]);
        break;

    case 0x17: /* JZ */
        trace_s5l8702_fmiss_branch("jz", dst_reg, vm->regs[dst_reg], imm);
        if (vm->regs[dst_reg] == 0) {
            vm->pc = vm->start_pc + imm;
            return true;
        }
        break;

    case 0x18: /* Load from NAND address given by register */
        uint32_t value = ops->read(opaque, vm->regs[src_reg], 4);
        vm->regs[dst_reg] = value;
        trace_s5l8702_fmiss_load_nand(dst_reg, src_reg, value);
        break;

    case 0x19: /* Store to NAND address given by register */
        trace_s5l8702_fmiss_store_nand(src_reg, dst_reg, vm->regs[src_reg], vm->regs[dst_reg]);
        ops->write(opaque, vm->regs[src_reg], vm->regs[dst_reg], 4);
        break;

    default:
        hw_error("s5l8702-nand: fmiss_vm unimplemented opcode 0x%02X\n", opcode);
        return false;
    }

    vm->pc += 8;
    return true;
}

void fmiss_vm_execute(const FmissNandOps *ops, void *opaque, fmiss_vm *vm) {
    trace_s5l8702_fmiss_start(vm->start_pc);
    while (fmiss_vm_step(ops, opaque, vm));
    trace_s5l8702_fmiss_done();
}

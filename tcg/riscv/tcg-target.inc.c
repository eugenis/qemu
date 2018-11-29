/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2018 SiFive, Inc
 * Copyright (c) 2008-2009 Arnaud Patard <arnaud.patard@rtp-net.org>
 * Copyright (c) 2009 Aurelien Jarno <aurelien@aurel32.net>
 * Copyright (c) 2008 Fabrice Bellard
 *
 * Based on i386/tcg-target.c and mips/tcg-target.c
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifdef CONFIG_DEBUG_TCG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "zero",
    "ra",
    "sp",
    "gp",
    "tp",
    "t0",
    "t1",
    "t2",
    "s0",
    "s1",
    "a0",
    "a1",
    "a2",
    "a3",
    "a4",
    "a5",
    "a6",
    "a7",
    "s2",
    "s3",
    "s4",
    "s5",
    "s6",
    "s7",
    "s8",
    "s9",
    "s10",
    "s11",
    "t3",
    "t4",
    "t5",
    "t6"
};
#endif

static const int tcg_target_reg_alloc_order[] = {
    /* Call saved registers */
    /* TCG_REG_S0 reservered for TCG_AREG0 */
    TCG_REG_S1,
    TCG_REG_S2,
    TCG_REG_S3,
    TCG_REG_S4,
    TCG_REG_S5,
    TCG_REG_S6,
    TCG_REG_S7,
    TCG_REG_S8,
    TCG_REG_S9,
    TCG_REG_S10,
    TCG_REG_S11,

    /* Call clobbered registers */
    TCG_REG_T0,
    TCG_REG_T1,
    TCG_REG_T2,
    TCG_REG_T3,
    TCG_REG_T4,
    TCG_REG_T5,
    TCG_REG_T6,

    /* Argument registers */
    TCG_REG_A0,
    TCG_REG_A1,
    TCG_REG_A2,
    TCG_REG_A3,
    TCG_REG_A4,
    TCG_REG_A5,
    TCG_REG_A6,
    TCG_REG_A7,
};

static const int tcg_target_call_iarg_regs[] = {
    TCG_REG_A0,
    TCG_REG_A1,
    TCG_REG_A2,
    TCG_REG_A3,
    TCG_REG_A4,
    TCG_REG_A5,
    TCG_REG_A6,
    TCG_REG_A7,
};

static const int tcg_target_call_oarg_regs[] = {
    TCG_REG_A0,
    TCG_REG_A1,
};

#if TCG_TARGET_REG_BITS == 64
#  define sextract_target sextract64
#else
#  define sextract_target sextract32
#endif

#define TCG_CT_CONST_ZERO  0x100
#define TCG_CT_CONST_S12   0x200
#define TCG_CT_CONST_N12   0x400

/* parse target specific constraints */
static const char *target_parse_constraint(TCGArgConstraint *ct,
                                           const char *ct_str, TCGType type)
{
    switch (*ct_str++) {
    case 'r':
        ct->ct |= TCG_CT_REG;
        ct->u.regs = 0xffffffff;
        break;
    case 'L':
        /* qemu_ld/qemu_st constraint */
        ct->ct |= TCG_CT_REG;
        ct->u.regs = 0xffffffff;
        /* qemu_ld/qemu_st uses TCG_REG_TMP0 */
#if defined(CONFIG_SOFTMMU)
        tcg_regset_reset_reg(ct->u.regs, tcg_target_call_iarg_regs[0]);
        tcg_regset_reset_reg(ct->u.regs, tcg_target_call_iarg_regs[1]);
        tcg_regset_reset_reg(ct->u.regs, tcg_target_call_iarg_regs[2]);
        tcg_regset_reset_reg(ct->u.regs, tcg_target_call_iarg_regs[3]);
        tcg_regset_reset_reg(ct->u.regs, tcg_target_call_iarg_regs[4]);
#endif
        break;
    case 'I':
        ct->ct |= TCG_CT_CONST_S12;
        break;
    case 'N':
        ct->ct |= TCG_CT_CONST_N12;
        break;
    case 'Z':
        /* we can use a zero immediate as a zero register argument. */
        ct->ct |= TCG_CT_CONST_ZERO;
        break;
    default:
        return NULL;
    }
    return ct_str;
}

/* test if a constant matches the constraint */
static int tcg_target_const_match(tcg_target_long val, TCGType type,
                                  const TCGArgConstraint *arg_ct)
{
    int ct = arg_ct->ct;
    if (ct & TCG_CT_CONST) {
        return 1;
    }
    if ((ct & TCG_CT_CONST_ZERO) && val == 0) {
        return 1;
    }
    if ((ct & TCG_CT_CONST_S12) && val == sextract_target(val, 0, 12)) {
        return 1;
    }
    if ((ct & TCG_CT_CONST_N12) && val == sextract_target(-val, 0, 12)) {
        return 1;
    }
    return 0;
}

/*
 * RISC-V Base ISA opcodes (IM)
 */

typedef enum {
    OPC_ADD = 0x33,
    OPC_ADDI = 0x13,
    OPC_ADDIW = 0x1b,
    OPC_ADDW = 0x3b,
    OPC_AND = 0x7033,
    OPC_ANDI = 0x7013,
    OPC_AUIPC = 0x17,
    OPC_BEQ = 0x63,
    OPC_BGE = 0x5063,
    OPC_BGEU = 0x7063,
    OPC_BLT = 0x4063,
    OPC_BLTU = 0x6063,
    OPC_BNE = 0x1063,
    OPC_DIV = 0x2004033,
    OPC_DIVU = 0x2005033,
    OPC_DIVUW = 0x200503b,
    OPC_DIVW = 0x200403b,
    OPC_JAL = 0x6f,
    OPC_JALR = 0x67,
    OPC_LB = 0x3,
    OPC_LBU = 0x4003,
    OPC_LD = 0x3003,
    OPC_LH = 0x1003,
    OPC_LHU = 0x5003,
    OPC_LUI = 0x37,
    OPC_LW = 0x2003,
    OPC_LWU = 0x6003,
    OPC_MUL = 0x2000033,
    OPC_MULH = 0x2001033,
    OPC_MULHSU = 0x2002033,
    OPC_MULHU = 0x2003033,
    OPC_MULW = 0x200003b,
    OPC_OR = 0x6033,
    OPC_ORI = 0x6013,
    OPC_REM = 0x2006033,
    OPC_REMU = 0x2007033,
    OPC_REMUW = 0x200703b,
    OPC_REMW = 0x200603b,
    OPC_SB = 0x23,
    OPC_SD = 0x3023,
    OPC_SH = 0x1023,
    OPC_SLL = 0x1033,
    OPC_SLLI = 0x1013,
    OPC_SLLIW = 0x101b,
    OPC_SLLW = 0x103b,
    OPC_SLT = 0x2033,
    OPC_SLTI = 0x2013,
    OPC_SLTIU = 0x3013,
    OPC_SLTU = 0x3033,
    OPC_SRA = 0x40005033,
    OPC_SRAI = 0x40005013,
    OPC_SRAIW = 0x4000501b,
    OPC_SRAW = 0x4000503b,
    OPC_SRL = 0x5033,
    OPC_SRLI = 0x5013,
    OPC_SRLIW = 0x501b,
    OPC_SRLW = 0x503b,
    OPC_SUB = 0x40000033,
    OPC_SUBW = 0x4000003b,
    OPC_SW = 0x2023,
    OPC_XOR = 0x4033,
    OPC_XORI = 0x4013,
    OPC_FENCE_R_R = 0x0220000f,
    OPC_FENCE_R_W = 0x0210000f,
    OPC_FENCE_R_RW = 0x0230000f,
    OPC_FENCE_W_R = 0x0120000f,
    OPC_FENCE_W_W = 0x0110000f,
    OPC_FENCE_W_RW = 0x0130000f,
    OPC_FENCE_RW_R = 0x0320000f,
    OPC_FENCE_RW_W = 0x0310000f,
    OPC_FENCE_RW_RW = 0x0330000f,
} RISCVInsn;

/*
 * RISC-V immediate and instruction encoders (excludes 16-bit RVC)
 */

/* Type-R */

static int32_t encode_r(RISCVInsn opc, TCGReg rd, TCGReg rs1, TCGReg rs2)
{
    return opc | (rd & 0x1f) << 7 | (rs1 & 0x1f) << 15 | (rs2 & 0x1f) << 20;
}

/* Type-I */

static int32_t encode_imm12(uint32_t imm)
{
    return (imm & 0xfff) << 20;
}

static int32_t encode_i(RISCVInsn opc, TCGReg rd, TCGReg rs1, uint32_t imm)
{
    return opc | (rd & 0x1f) << 7 | (rs1 & 0x1f) << 15 | encode_imm12(imm);
}

/* Type-S */

static int32_t encode_simm12(uint32_t imm)
{
    int32_t ret = 0;

    ret |= (imm & 0xFE0) << 20;
    ret |= (imm & 0x1F) << 7;

    return ret;
}

static int32_t encode_s(RISCVInsn opc, TCGReg rs1, TCGReg rs2, uint32_t imm)
{
    return opc | (rs1 & 0x1f) << 15 | (rs2 & 0x1f) << 20 | encode_simm12(imm);
}

/* Type-SB */

static int32_t encode_sbimm12(uint32_t imm)
{
    int32_t ret = 0;

    ret |= (imm & 0x1000) << 19;
    ret |= (imm & 0x7e0) << 20;
    ret |= (imm & 0x1e) << 7;
    ret |= (imm & 0x800) >> 4;

    return ret;
}

static int32_t encode_sb(RISCVInsn opc, TCGReg rs1, TCGReg rs2, uint32_t imm)
{
    return opc | (rs1 & 0x1f) << 15 | (rs2 & 0x1f) << 20 | encode_sbimm12(imm);
}

/* Type-U */

static int32_t encode_uimm20(uint32_t imm)
{
    return imm & 0xfffff000;
}

static int32_t encode_u(RISCVInsn opc, TCGReg rd, uint32_t imm)
{
    return opc | (rd & 0x1f) << 7 | encode_uimm20(imm);
}

/* Type-UJ */

static int32_t encode_ujimm12(uint32_t imm)
{
    int32_t ret = 0;

    ret |= (imm & 0x100000) << 11;
    ret |= (imm & 0xffe) << 20;
    ret |= (imm & 0x800) << 9;
    ret |= imm & 0xff000;

    return ret;
}

static int32_t encode_uj(RISCVInsn opc, TCGReg rd, uint32_t imm)
{
    return opc | (rd & 0x1f) << 7 | encode_ujimm12(imm);
}

/*
 * RISC-V instruction emitters
 */

static void tcg_out_opc_reg(TCGContext *s, RISCVInsn opc,
                            TCGReg rd, TCGReg rs1, TCGReg rs2)
{
    tcg_out32(s, encode_r(opc, rd, rs1, rs2));
}

static void tcg_out_opc_imm(TCGContext *s, RISCVInsn opc,
                            TCGReg rd, TCGReg rs1, TCGArg imm)
{
    tcg_out32(s, encode_i(opc, rd, rs1, imm));
}

static void tcg_out_opc_store(TCGContext *s, RISCVInsn opc,
                              TCGReg rs1, TCGReg rs2, uint32_t imm)
{
    tcg_out32(s, encode_s(opc, rs1, rs2, imm));
}

static void tcg_out_opc_branch(TCGContext *s, RISCVInsn opc,
                               TCGReg rs1, TCGReg rs2, uint32_t imm)
{
    tcg_out32(s, encode_sb(opc, rs1, rs2, imm));
}

static void tcg_out_opc_upper(TCGContext *s, RISCVInsn opc,
                              TCGReg rd, uint32_t imm)
{
    tcg_out32(s, encode_u(opc, rd, imm));
}

static void tcg_out_opc_jump(TCGContext *s, RISCVInsn opc,
                             TCGReg rd, uint32_t imm)
{
    tcg_out32(s, encode_uj(opc, rd, imm));
}

/*
 * Relocations
 */

static void reloc_sbimm12(tcg_insn_unit *code_ptr, tcg_insn_unit *target)
{
    intptr_t offset = (intptr_t)target - (intptr_t)code_ptr;
    tcg_debug_assert(offset == sextract_target(offset, 1, 12) << 1);

    code_ptr[0] |= encode_sbimm12(offset);
}

static void reloc_jimm20(tcg_insn_unit *code_ptr, tcg_insn_unit *target)
{
    intptr_t offset = (intptr_t)target - (intptr_t)code_ptr;
    tcg_debug_assert(offset == sextract_target(offset, 1, 20) << 1);

    code_ptr[0] |= encode_ujimm12(offset);
}

static void reloc_call(tcg_insn_unit *code_ptr, tcg_insn_unit *target)
{
    intptr_t offset = (intptr_t)target - (intptr_t)code_ptr;
    tcg_debug_assert(offset == (int32_t)offset);

    int32_t hi20 = ((offset + 0x800) >> 12) << 12;
    int32_t lo12 = offset - hi20;

    code_ptr[0] |= encode_uimm20(hi20);
    code_ptr[1] |= encode_imm12(lo12);
}

static void patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    uint32_t insn = *code_ptr;
    intptr_t diff;
    bool short_jmp;

    tcg_debug_assert(addend == 0);

    switch (type) {
    case R_RISCV_BRANCH:
        diff = value - (uintptr_t)code_ptr;
        short_jmp = diff == sextract_target(diff, 0, 12);

        if (short_jmp) {
            reloc_sbimm12(code_ptr, (tcg_insn_unit *)value);
        } else {
            /* Invert the condition */
            insn = insn ^ (1 << 12);
            /* Clear the offset */
            insn &= 0xFFF;
            /* Set the offset to the PC + 8 */
            reloc_sbimm12(code_ptr, code_ptr + 2);

            /* Move forward */
            code_ptr++;
            insn = *code_ptr;

            /* Overwrite the NOP with jal x0,value */
            code_ptr[1] = encode_uj(OPC_JAL, TCG_REG_ZERO, 0);
            reloc_jimm20(code_ptr + 1, (tcg_insn_unit *)value);
        }
        break;
    case R_RISCV_JAL:
        reloc_jimm20(code_ptr, (tcg_insn_unit *)value);
        break;
    case R_RISCV_CALL:
        reloc_call(code_ptr, (tcg_insn_unit *)value);
        break;
    default:
        tcg_abort();
    }
}

/*
 * TCG intrinsics
 */

static void tcg_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    if (ret == arg) {
        return;
    }
    switch (type) {
    case TCG_TYPE_I32:
    case TCG_TYPE_I64:
        tcg_out_opc_imm(s, OPC_ADDI, ret, arg, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_movi(TCGContext *s, TCGType type, TCGReg rd,
                         tcg_target_long val)
{
    tcg_target_long lo = sextract_target(val, 0, 12);
    tcg_target_long hi = val - lo;
    int shift;
    tcg_target_long tmp;
    RISCVInsn add32_op = TCG_TARGET_REG_BITS == 64 ? OPC_ADDIW : OPC_ADDI;
    ptrdiff_t offset = tcg_pcrel_diff(s, (void *)val);

    if (val == lo) {
        tcg_out_opc_imm(s, OPC_ADDI, rd, TCG_REG_ZERO, val);
    } else if (TCG_TARGET_REG_BITS == 32 || val == (int32_t)val) {
        tcg_out_opc_upper(s, OPC_LUI, rd, hi);
        if (lo != 0) {
            tcg_out_opc_imm(s, add32_op, rd, rd, lo);
        }

        return;
    }

    /* We can only be here if TCG_TARGET_REG_BITS != 32 */
    if (offset == sextract_target(offset, 1, 31) << 1) {
        tcg_out_opc_upper(s, OPC_AUIPC, rd, 0);
        tcg_out_opc_imm(s, OPC_ADDI, rd, rd, 0);
        reloc_call(s->code_ptr - 2, (tcg_insn_unit *)val);
        return;
    }

    shift = ctz64(val);
    tmp = val >> shift;

    if (tmp == sextract_target(tmp, 0, 12)) {
        tcg_out_opc_imm(s, OPC_ADDI, rd, TCG_REG_ZERO, tmp);
        tcg_out_opc_imm(s, OPC_SLLI, rd, rd, ctz64(val));
    } else if (!(val >> 31 == 0 || val >> 31 == -1)) {
        shift = ctz64(hi);
        hi >>= shift;
        tcg_out_movi(s, type, rd, hi);
        tcg_out_opc_imm(s, OPC_SLLI, rd, rd, shift);
        if (lo != 0) {
            tcg_out_opc_imm(s, OPC_ADDI, rd, rd, lo);
        }
    }
}

static void tcg_out_ext8u(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_imm(s, OPC_ANDI, ret, arg, 0xff);
}

static void tcg_out_ext16u(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_imm(s, OPC_SLLI, ret, arg, TCG_TARGET_REG_BITS - 16);
    tcg_out_opc_imm(s, OPC_SRLI, ret, ret, TCG_TARGET_REG_BITS - 16);
}

static void tcg_out_ext32u(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_imm(s, OPC_SLLI, ret, arg, 32);
    tcg_out_opc_imm(s, OPC_SRLI, ret, ret, 32);
}

static void tcg_out_ext8s(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_imm(s, OPC_SLLI, ret, arg, TCG_TARGET_REG_BITS - 8);
    tcg_out_opc_imm(s, OPC_SRAI, ret, ret, TCG_TARGET_REG_BITS - 8);
}

static void tcg_out_ext16s(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_imm(s, OPC_SLLI, ret, arg, TCG_TARGET_REG_BITS - 16);
    tcg_out_opc_imm(s, OPC_SRAI, ret, ret, TCG_TARGET_REG_BITS - 16);
}

static void tcg_out_ext32s(TCGContext *s, TCGReg ret, TCGReg arg)
{
    tcg_out_opc_imm(s, OPC_ADDIW, ret, arg, 0);
}

static void tcg_out_ldst(TCGContext *s, RISCVInsn opc, TCGReg data,
                         TCGReg addr, intptr_t offset)
{
    int32_t imm12 = sextract_target(offset, 0, 12);

    if (offset != imm12) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_TMP2, offset - imm12);
        if (addr != TCG_REG_ZERO) {
            tcg_out_opc_reg(s, OPC_ADD, TCG_REG_TMP2, TCG_REG_TMP2, addr);
        }
        addr = TCG_REG_TMP2;
    }
    switch (opc) {
    case OPC_SB:
    case OPC_SH:
    case OPC_SW:
    case OPC_SD:
        tcg_out_opc_store(s, opc, addr, data, imm12);
        break;
    case OPC_LB:
    case OPC_LBU:
    case OPC_LH:
    case OPC_LHU:
    case OPC_LW:
    case OPC_LWU:
    case OPC_LD:
        tcg_out_opc_imm(s, opc, data, addr, imm12);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_ld(TCGContext *s, TCGType type, TCGReg arg,
                       TCGReg arg1, intptr_t arg2)
{
    bool is32bit = (TCG_TARGET_REG_BITS == 32 || type == TCG_TYPE_I32);
    tcg_out_ldst(s, is32bit ? OPC_LW : OPC_LD, arg, arg1, arg2);
}

static void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg,
                       TCGReg arg1, intptr_t arg2)
{
    bool is32bit = (TCG_TARGET_REG_BITS == 32 || type == TCG_TYPE_I32);
    tcg_out_ldst(s, is32bit ? OPC_SW : OPC_SD, arg, arg1, arg2);
}

static bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                        TCGReg base, intptr_t ofs)
{
    if (val == 0) {
        tcg_out_st(s, type, TCG_REG_ZERO, base, ofs);
        return true;
    }
    return false;
}

static void tcg_out_addsub2(TCGContext *s,
                            TCGReg rl, TCGReg rh,
                            TCGReg al, TCGReg ah,
                            TCGReg bl, TCGReg bh,
                            bool cbl, bool cbh, bool is_sub)
{
    TCGReg th = TCG_REG_TMP1;

    /* If we have a negative constant such that negating it would
       make the high part zero, we can (usually) eliminate one insn.  */
    if (cbl && cbh && bh == -1 && bl != 0) {
        bl = -bl;
        bh = 0;
        is_sub = !is_sub;
    }

    /* By operating on the high part first, we get to use the final
       carry operation to move back from the temporary.  */
    if (!cbh) {
        tcg_out_opc_reg(s, (is_sub ? OPC_SUB : OPC_ADDI), th, ah, bh);
    } else if (bh != 0 || ah == rl) {
        tcg_out_opc_imm(s, OPC_ADDI, th, ah, (is_sub ? -bh : bh));
    } else {
        th = ah;
    }

    if (is_sub) {
        if (cbl) {
            tcg_out_opc_imm(s, OPC_SLTIU, TCG_REG_TMP0, al, bl);
            tcg_out_opc_imm(s, OPC_ADDI, rl, al, -bl);
        } else {
            tcg_out_opc_reg(s, OPC_SLTIU, TCG_REG_TMP0, al, bl);
            tcg_out_opc_reg(s, OPC_SUB, rl, al, bl);
        }
        tcg_out_opc_reg(s, OPC_SUB, rh, th, TCG_REG_TMP0);
    } else {
        if (cbl) {
            tcg_out_opc_imm(s, OPC_ADDI, rl, al, bl);
            tcg_out_opc_imm(s, OPC_SLTIU, TCG_REG_TMP0, rl, bl);
        } else if (rl == al && rl == bl) {
            tcg_out_opc_imm(s, OPC_SRLI, TCG_REG_TMP0, al,
                            TCG_TARGET_REG_BITS - 1);
            tcg_out_opc_reg(s, OPC_ADD, rl, al, bl);
        } else {
            tcg_out_opc_reg(s, OPC_ADD, rl, al, bl);
            tcg_out_opc_reg(s, OPC_SLTIU, TCG_REG_TMP0, rl,
                            (rl == bl ? al : bl));
        }
        tcg_out_opc_reg(s, OPC_ADD, rh, th, TCG_REG_TMP0);
    }
}

static const struct {
    RISCVInsn op;
    bool swap;
} tcg_brcond_to_riscv[] = {
    [TCG_COND_EQ] =  { OPC_BEQ,  false },
    [TCG_COND_NE] =  { OPC_BNE,  false },
    [TCG_COND_LT] =  { OPC_BLT,  false },
    [TCG_COND_GE] =  { OPC_BGE,  false },
    [TCG_COND_LE] =  { OPC_BGE,  true  },
    [TCG_COND_GT] =  { OPC_BLT,  true  },
    [TCG_COND_LTU] = { OPC_BLTU, false },
    [TCG_COND_GEU] = { OPC_BGEU, false },
    [TCG_COND_LEU] = { OPC_BGEU, true  },
    [TCG_COND_GTU] = { OPC_BLTU, true  }
};

static void tcg_out_brcond(TCGContext *s, TCGCond cond, TCGReg arg1,
                           TCGReg arg2, TCGLabel *l)
{
    RISCVInsn op = tcg_brcond_to_riscv[cond].op;
    intptr_t diff;
    bool swap = tcg_brcond_to_riscv[cond].swap;
    bool short_jmp;

    tcg_debug_assert(op != 0);

    tcg_out_opc_branch(s, op, swap ? arg2 : arg1, swap ? arg1 : arg2, 0);

    diff = (intptr_t)l->u.value_ptr - (intptr_t)s->code_gen_ptr;
    short_jmp = diff == sextract_target(diff, 0, 12);

    if (l->has_value && short_jmp) {
        reloc_sbimm12(s->code_ptr - 1, l->u.value_ptr);
    } else {
        tcg_out_reloc(s, s->code_ptr - 1, R_RISCV_BRANCH, l, 0);
        /* NOP to allow patching later */
        tcg_out_opc_imm(s, OPC_ADDI, TCG_REG_ZERO, TCG_REG_ZERO, 0);
    }
}

static void tcg_out_setcond(TCGContext *s, TCGCond cond, TCGReg ret,
                            TCGReg arg1, TCGReg arg2)
{
    switch (cond) {
    case TCG_COND_EQ:
        tcg_out_opc_reg(s, OPC_SUB, ret, arg1, arg2);
        tcg_out_opc_imm(s, OPC_SLTIU, ret, ret, 1);
        break;
    case TCG_COND_NE:
        tcg_out_opc_reg(s, OPC_SUB, ret, arg1, arg2);
        tcg_out_opc_reg(s, OPC_SLTU, ret, TCG_REG_ZERO, ret);
        break;
    case TCG_COND_LT:
        tcg_out_opc_reg(s, OPC_SLT, ret, arg1, arg2);
        break;
    case TCG_COND_GE:
        tcg_out_opc_reg(s, OPC_SLT, ret, arg1, arg2);
        tcg_out_opc_imm(s, OPC_XORI, ret, ret, 1);
        break;
    case TCG_COND_LE:
        tcg_out_opc_reg(s, OPC_SLT, ret, arg2, arg1);
        tcg_out_opc_imm(s, OPC_XORI, ret, ret, 1);
        break;
    case TCG_COND_GT:
        tcg_out_opc_reg(s, OPC_SLT, ret, arg2, arg1);
        break;
    case TCG_COND_LTU:
        tcg_out_opc_reg(s, OPC_SLTU, ret, arg1, arg2);
        break;
    case TCG_COND_GEU:
        tcg_out_opc_reg(s, OPC_SLTU, ret, arg1, arg2);
        tcg_out_opc_imm(s, OPC_XORI, ret, ret, 1);
        break;
    case TCG_COND_LEU:
        tcg_out_opc_reg(s, OPC_SLTU, ret, arg2, arg1);
        tcg_out_opc_imm(s, OPC_XORI, ret, ret, 1);
        break;
    case TCG_COND_GTU:
        tcg_out_opc_reg(s, OPC_SLTU, ret, arg2, arg1);
        break;
    default:
         g_assert_not_reached();
         break;
     }
}

static void tcg_out_brcond2(TCGContext *s, TCGCond cond, TCGReg al, TCGReg ah,
                            TCGReg bl, TCGReg bh, TCGLabel *l)
{
    /* todo */
    g_assert_not_reached();
}

static void tcg_out_setcond2(TCGContext *s, TCGCond cond, TCGReg ret,
                             TCGReg al, TCGReg ah, TCGReg bl, TCGReg bh)
{
    /* todo */
    g_assert_not_reached();
}

static inline void tcg_out_goto(TCGContext *s, tcg_insn_unit *target)
{
    ptrdiff_t offset = tcg_pcrel_diff(s, target);
    tcg_debug_assert(offset == sextract_target(offset, 0, 26));
    tcg_out_opc_jump(s, OPC_JAL, TCG_REG_ZERO, offset);
}

static void tcg_out_call_int(TCGContext *s, tcg_insn_unit *arg, bool tail)
{
    TCGReg link = tail ? TCG_REG_ZERO : TCG_REG_RA;
    ptrdiff_t offset = tcg_pcrel_diff(s, arg);

    if (offset == sextract_target(offset, 1, 26) << 1) {
        /* short jump: -2097150 to 2097152 */
        tcg_out_opc_jump(s, OPC_JAL, link, offset);
    } else if (TCG_TARGET_REG_BITS == 32 ||
        offset == sextract_target(offset, 1, 31) << 1) {
        /* long jump: -2147483646 to 2147483648 */
        tcg_out_opc_upper(s, OPC_AUIPC, TCG_REG_TMP0, 0);
        tcg_out_opc_imm(s, OPC_JALR, link, TCG_REG_TMP0, 0);
        reloc_call(s->code_ptr - 2, arg);
    } else if (TCG_TARGET_REG_BITS == 64) {
        /* far jump: 64-bit */
        tcg_target_long imm = sextract_target((tcg_target_long)arg, 0, 12);
        tcg_target_long base = (tcg_target_long)arg - imm;
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_TMP0, base);
        tcg_out_opc_imm(s, OPC_JALR, link, TCG_REG_TMP0, imm);
    } else {
        g_assert_not_reached();
    }
}

static void tcg_out_call(TCGContext *s, tcg_insn_unit *arg)
{
    tcg_out_call_int(s, arg, false);
}

static void tcg_out_mb(TCGContext *s, TCGArg a0)
{
    static const RISCVInsn fence[] = {
        [0 ... TCG_MO_ALL] = OPC_FENCE_RW_RW,
        [TCG_MO_LD_LD]     = OPC_FENCE_R_R,
        [TCG_MO_ST_LD]     = OPC_FENCE_W_R,
        [TCG_MO_LD_ST]     = OPC_FENCE_R_W,
        [TCG_MO_ST_ST]     = OPC_FENCE_W_W,
    };
    tcg_out32(s, fence[a0 & TCG_MO_ALL]);
}

/*
 * Load/store and TLB
 */

#if defined(CONFIG_SOFTMMU)
#include "tcg-ldst.inc.c"

/* helper signature: helper_ret_ld_mmu(CPUState *env, target_ulong addr,
 *                                     TCGMemOpIdx oi, uintptr_t ra)
 */
static void * const qemu_ld_helpers[16] = {
    [MO_UB]   = helper_ret_ldub_mmu,
    [MO_SB]   = helper_ret_ldsb_mmu,
    [MO_LEUW] = helper_le_lduw_mmu,
    [MO_LESW] = helper_le_ldsw_mmu,
    [MO_LEUL] = helper_le_ldul_mmu,
#if TCG_TARGET_REG_BITS == 64
    [MO_LESL] = helper_le_ldsl_mmu,
#endif
    [MO_LEQ]  = helper_le_ldq_mmu,
    [MO_BEUW] = helper_be_lduw_mmu,
    [MO_BESW] = helper_be_ldsw_mmu,
    [MO_BEUL] = helper_be_ldul_mmu,
#if TCG_TARGET_REG_BITS == 64
    [MO_BESL] = helper_be_ldsl_mmu,
#endif
    [MO_BEQ]  = helper_be_ldq_mmu,
};

/* helper signature: helper_ret_st_mmu(CPUState *env, target_ulong addr,
 *                                     uintxx_t val, TCGMemOpIdx oi,
 *                                     uintptr_t ra)
 */
static void * const qemu_st_helpers[16] = {
    [MO_UB]   = helper_ret_stb_mmu,
    [MO_LEUW] = helper_le_stw_mmu,
    [MO_LEUL] = helper_le_stl_mmu,
    [MO_LEQ]  = helper_le_stq_mmu,
    [MO_BEUW] = helper_be_stw_mmu,
    [MO_BEUL] = helper_be_stl_mmu,
    [MO_BEQ]  = helper_be_stq_mmu,
};

static void tcg_out_tlb_load(TCGContext *s, TCGReg addrl,
                             TCGReg addrh, TCGMemOpIdx oi,
                             tcg_insn_unit **label_ptr, bool is_load)
{
    TCGMemOp opc = get_memop(oi);
    unsigned s_bits = opc & MO_SIZE;
    unsigned a_bits = get_alignment_bits(opc);
    target_ulong mask;
    int mem_index = get_mmuidx(oi);
    int cmp_off
        = (is_load
           ? offsetof(CPUArchState, tlb_table[mem_index][0].addr_read)
           : offsetof(CPUArchState, tlb_table[mem_index][0].addr_write));
    int add_off = offsetof(CPUArchState, tlb_table[mem_index][0].addend);
    RISCVInsn load_cmp_op = (TARGET_LONG_BITS == 64 ? OPC_LD :
                             TCG_TARGET_REG_BITS == 64 ? OPC_LWU : OPC_LW);
    TCGReg base = TCG_AREG0;

    /* We don't support oversize guests */
    if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
        g_assert_not_reached();
    }

    /* We don't support unaligned accesses. */
    if (a_bits < s_bits) {
        a_bits = s_bits;
    }
    mask = (target_ulong)TARGET_PAGE_MASK | ((1 << a_bits) - 1);


    /* Compensate for very large offsets.  */
    if (add_off >= 0x1000) {
        int adj;
        base = TCG_REG_TMP2;
        if (cmp_off <= 2 * 0xfff) {
            adj = 0xfff;
            tcg_out_opc_imm(s, OPC_ADDI, base, TCG_AREG0, adj);
        } else {
            adj = cmp_off - sextract_target(cmp_off, 0, 12);
            tcg_debug_assert(add_off - adj >= -0x1000
                             && add_off - adj < 0x1000);

            tcg_out_opc_upper(s, OPC_LUI, base, adj);
            tcg_out_opc_reg(s, OPC_ADD, base, base, TCG_AREG0);
        }
        add_off -= adj;
        cmp_off -= adj;
    }

    /* Extract the page index.  */
    if (CPU_TLB_BITS + CPU_TLB_ENTRY_BITS < 12) {
        tcg_out_opc_imm(s, OPC_SRLI, TCG_REG_TMP0, addrl,
                        TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);
        tcg_out_opc_imm(s, OPC_ANDI, TCG_REG_TMP0, TCG_REG_TMP0,
                        MAKE_64BIT_MASK(CPU_TLB_ENTRY_BITS, CPU_TLB_BITS));
    } else {
        tcg_out_opc_imm(s, OPC_SRLI, TCG_REG_TMP0, addrl, TARGET_PAGE_BITS);
        tcg_out_opc_imm(s, OPC_ANDI, TCG_REG_TMP0, TCG_REG_TMP0,
                        MAKE_64BIT_MASK(0, CPU_TLB_BITS));
        tcg_out_opc_imm(s, OPC_SLLI, TCG_REG_TMP0, TCG_REG_TMP0,
                        CPU_TLB_ENTRY_BITS);
    }

    /* Add that to the base address to index the tlb.  */
    tcg_out_opc_reg(s, OPC_ADD, TCG_REG_TMP2, base, TCG_REG_TMP0);
    base = TCG_REG_TMP2;

    /* Load the tlb comparator and the addend.  */
    tcg_out_ldst(s, load_cmp_op, TCG_REG_TMP0, base, cmp_off);
    tcg_out_ldst(s, load_cmp_op, TCG_REG_TMP2, base, add_off);

    /* Clear the non-page, non-alignment bits from the address.  */
    if (mask == sextract_target(mask, 0, 12)) {
        tcg_out_opc_imm(s, OPC_ANDI, TCG_REG_TMP1, addrl, mask);
    } else {
        tcg_out_movi(s, TCG_TYPE_REG, TCG_REG_TMP1, mask);
        tcg_out_opc_reg(s, OPC_AND, TCG_REG_TMP1, TCG_REG_TMP1, addrl);
     }

    /* Compare masked address with the TLB entry. */
    label_ptr[0] = s->code_ptr;
    tcg_out_opc_branch(s, OPC_BNE, TCG_REG_TMP0, TCG_REG_TMP1, 0);
    /* TODO: Move this out of line
     * see:
     *   https://lists.nongnu.org/archive/html/qemu-devel/2018-11/msg02234.html
     */

    /* TLB Hit - translate address using addend.  */
    if (TCG_TARGET_REG_BITS > TARGET_LONG_BITS) {
        tcg_out_ext32u(s, TCG_REG_TMP0, addrl);
        addrl = TCG_REG_TMP0;
    }
    tcg_out_opc_reg(s, OPC_ADD, TCG_REG_TMP0, TCG_REG_TMP2, addrl);
}

static void add_qemu_ldst_label(TCGContext *s, int is_ld, TCGMemOpIdx oi,
                                TCGType ext,
                                TCGReg datalo, TCGReg datahi,
                                TCGReg addrlo, TCGReg addrhi,
                                void *raddr, tcg_insn_unit **label_ptr)
{
    TCGLabelQemuLdst *label = new_ldst_label(s);

    label->is_ld = is_ld;
    label->oi = oi;
    label->type = ext;
    label->datalo_reg = datalo;
    label->datahi_reg = datahi;
    label->addrlo_reg = addrlo;
    label->addrhi_reg = addrhi;
    label->raddr = raddr;
    label->label_ptr[0] = label_ptr[0];
}

static void tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *l)
{
    TCGMemOpIdx oi = l->oi;
    TCGMemOp opc = get_memop(oi);
    TCGReg a0 = tcg_target_call_iarg_regs[0];
    TCGReg a1 = tcg_target_call_iarg_regs[1];
    TCGReg a2 = tcg_target_call_iarg_regs[2];
    TCGReg a3 = tcg_target_call_iarg_regs[3];

    /* We don't support oversize guests */
    if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
        g_assert_not_reached();
    }

    /* resolve label address */
    reloc_sbimm12(l->label_ptr[0], s->code_ptr);

    /* call load helper */
    tcg_out_mov(s, TCG_TYPE_PTR, a0, TCG_AREG0);
    tcg_out_mov(s, TCG_TYPE_PTR, a1, l->addrlo_reg);
    tcg_out_movi(s, TCG_TYPE_PTR, a2, oi);
    tcg_out_movi(s, TCG_TYPE_PTR, a3, (tcg_target_long)l->raddr);

    tcg_out_call(s, qemu_ld_helpers[opc & (MO_BSWAP | MO_SSIZE)]);
    tcg_out_mov(s, (opc & MO_SIZE) == MO_64, l->datalo_reg, a0);

    tcg_out_goto(s, l->raddr);
}

static void tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *l)
{
    TCGMemOpIdx oi = l->oi;
    TCGMemOp opc = get_memop(oi);
    TCGMemOp s_bits = opc & MO_SIZE;
    TCGReg a0 = tcg_target_call_iarg_regs[0];
    TCGReg a1 = tcg_target_call_iarg_regs[1];
    TCGReg a2 = tcg_target_call_iarg_regs[2];
    TCGReg a3 = tcg_target_call_iarg_regs[3];
    TCGReg a4 = tcg_target_call_iarg_regs[4];

    /* We don't support oversize guests */
    if (TCG_TARGET_REG_BITS < TARGET_LONG_BITS) {
        g_assert_not_reached();
    }

    /* resolve label address */
    reloc_sbimm12(l->label_ptr[0], s->code_ptr);

    /* call store helper */
    tcg_out_mov(s, TCG_TYPE_PTR, a0, TCG_AREG0);
    tcg_out_mov(s, TCG_TYPE_PTR, a1, l->addrlo_reg);
    tcg_out_mov(s, TCG_TYPE_PTR, a2, l->datalo_reg);
    switch (s_bits) {
    case MO_8:
        tcg_out_ext8u(s, a2, a2);
        break;
    case MO_16:
        tcg_out_ext16u(s, a2, a2);
        break;
    default:
        break;
    }
    tcg_out_movi(s, TCG_TYPE_PTR, a3, oi);
    tcg_out_movi(s, TCG_TYPE_PTR, a4, (tcg_target_long)l->raddr);

    tcg_out_call(s, qemu_st_helpers[opc & (MO_BSWAP | MO_SSIZE)]);

    tcg_out_goto(s, l->raddr);
}
#endif /* CONFIG_SOFTMMU */

static void tcg_out_qemu_ld_direct(TCGContext *s, TCGReg lo, TCGReg hi,
                                   TCGReg base, TCGMemOp opc, bool is_64)
{
    const TCGMemOp bswap = opc & MO_BSWAP;

    /* We don't yet handle byteswapping, assert */
    g_assert(!bswap);

    switch (opc & (MO_SSIZE)) {
    case MO_UB:
        tcg_out_opc_imm(s, OPC_LBU, lo, base, 0);
        break;
    case MO_SB:
        tcg_out_opc_imm(s, OPC_LB, lo, base, 0);
        break;
    case MO_UW:
        tcg_out_opc_imm(s, OPC_LHU, lo, base, 0);
        break;
    case MO_SW:
        tcg_out_opc_imm(s, OPC_LH, lo, base, 0);
        break;
    case MO_UL:
        if (TCG_TARGET_REG_BITS == 64 && is_64) {
            tcg_out_opc_imm(s, OPC_LWU, lo, base, 0);
            break;
        }
        /* FALLTHRU */
    case MO_SL:
        tcg_out_opc_imm(s, OPC_LW, lo, base, 0);
        break;
    case MO_Q:
        /* Prefer to load from offset 0 first, but allow for overlap.  */
        if (TCG_TARGET_REG_BITS == 64) {
            tcg_out_opc_imm(s, OPC_LD, lo, base, 0);
        } else if (lo != base) {
            tcg_out_opc_imm(s, OPC_LW, lo, base, 0);
            tcg_out_opc_imm(s, OPC_LW, hi, base, 4);
        } else {
            tcg_out_opc_imm(s, OPC_LW, hi, base, 4);
            tcg_out_opc_imm(s, OPC_LW, lo, base, 0);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args, bool is_64)
{
    TCGReg addr_regl, addr_regh __attribute__((unused));
    TCGReg data_regl, data_regh;
    TCGMemOpIdx oi;
    TCGMemOp opc;
#if defined(CONFIG_SOFTMMU)
    tcg_insn_unit *label_ptr[1];
#endif
    TCGReg base = TCG_REG_TMP0;

    data_regl = *args++;
    data_regh = (TCG_TARGET_REG_BITS == 32 && is_64 ? *args++ : 0);
    addr_regl = *args++;
    addr_regh = (TCG_TARGET_REG_BITS < TARGET_LONG_BITS ? *args++ : 0);
    oi = *args++;
    opc = get_memop(oi);

#if defined(CONFIG_SOFTMMU)
    tcg_out_tlb_load(s, addr_regl, addr_regh, oi, label_ptr, 1);
    tcg_out_qemu_ld_direct(s, data_regl, data_regh, base, opc, is_64);
    add_qemu_ldst_label(s, 1, oi,
                        (is_64 ? TCG_TYPE_I64 : TCG_TYPE_I32),
                        data_regl, data_regh, addr_regl, addr_regh,
                        s->code_ptr, label_ptr);
#else
    if (TCG_TARGET_REG_BITS > TARGET_LONG_BITS) {
        tcg_out_ext32u(s, base, addr_regl);
        addr_regl = base;
    }

    if (guest_base == 0) {
        tcg_out_opc_reg(s, OPC_ADD, base, addr_regl, TCG_REG_ZERO);
    } else {
        tcg_out_opc_reg(s, OPC_ADD, base, TCG_GUEST_BASE_REG, addr_regl);
    }
    tcg_out_qemu_ld_direct(s, data_regl, data_regh, base, opc, is_64);
#endif
}

static void tcg_out_qemu_st_direct(TCGContext *s, TCGReg lo, TCGReg hi,
                                   TCGReg base, TCGMemOp opc)
{
    const TCGMemOp bswap = opc & MO_BSWAP;

    /* We don't yet handle byteswapping, assert */
    g_assert(!bswap);

    switch (opc & (MO_SSIZE)) {
    case MO_8:
        tcg_out_opc_store(s, OPC_SB, base, lo, 0);
        break;
    case MO_16:
        tcg_out_opc_store(s, OPC_SH, base, lo, 0);
        break;
    case MO_32:
        tcg_out_opc_store(s, OPC_SW, base, lo, 0);
        break;
    case MO_64:
        if (TCG_TARGET_REG_BITS == 64) {
            tcg_out_opc_store(s, OPC_SD, base, lo, 0);
        } else {
            tcg_out_opc_store(s, OPC_SW, base, lo, 0);
            tcg_out_opc_store(s, OPC_SW, base, hi, 4);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static void tcg_out_qemu_st(TCGContext *s, const TCGArg *args, bool is_64)
{
    TCGReg addr_regl, addr_regh __attribute__((unused));
    TCGReg data_regl, data_regh;
    TCGMemOpIdx oi;
    TCGMemOp opc;
#if defined(CONFIG_SOFTMMU)
    tcg_insn_unit *label_ptr[1];
#endif
    TCGReg base = TCG_REG_TMP0;

    data_regl = *args++;
    data_regh = (TCG_TARGET_REG_BITS == 32 && is_64 ? *args++ : 0);
    addr_regl = *args++;
    addr_regh = (TCG_TARGET_REG_BITS < TARGET_LONG_BITS ? *args++ : 0);
    oi = *args++;
    opc = get_memop(oi);

#if defined(CONFIG_SOFTMMU)
    tcg_out_tlb_load(s, addr_regl, addr_regh, oi, label_ptr, 0);
    tcg_out_qemu_st_direct(s, data_regl, data_regh, base, opc);
    add_qemu_ldst_label(s, 0, oi,
                        (is_64 ? TCG_TYPE_I64 : TCG_TYPE_I32),
                        data_regl, data_regh, addr_regl, addr_regh,
                        s->code_ptr, label_ptr);
#else
    if (TCG_TARGET_REG_BITS > TARGET_LONG_BITS) {
        tcg_out_ext32u(s, base, addr_regl);
        addr_regl = base;
    }

    if (guest_base == 0) {
        tcg_out_opc_reg(s, OPC_ADD, base, addr_regl, TCG_REG_ZERO);
    } else {
        tcg_out_opc_reg(s, OPC_ADD, base, TCG_GUEST_BASE_REG, addr_regl);
    }
    tcg_out_qemu_st_direct(s, data_regl, data_regh, base, opc);
#endif
}

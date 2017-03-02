/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2009, 2011 Stefan Weil
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

#include "qemu/tci.h"

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_F, TCG_REG_E, TCG_REG_D, TCG_REG_C, TCG_REG_B, TCG_REG_A
};

/* No call arguments via registers.  All will be stored on the "stack".  */
static const int tcg_target_call_iarg_regs[] = { };

static const int tcg_target_call_oarg_regs[] = {
    TCG_REG_A,
#if TCG_TARGET_REG_BITS == 32
    TCG_REG_B
#endif
};

#ifdef CONFIG_DEBUG_TCG
static const char *const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "a", "b", "c", "d", "e", "f", "vp", "sp"
};
#endif /* NDEBUG */

static void patch_reloc(tcg_insn_unit *ptr, int type,
                        intptr_t value, intptr_t addend)
{
    intptr_t disp = (tcg_insn_unit *)value - (ptr + 1);

    tcg_debug_assert(disp >= MIN_Y && disp <= MAX_Y);
    *ptr = deposit32(*ptr, POS_Y, LEN_Y, disp + BIAS_Y);
}

/* Constants we accept.  */
#define TCG_CT_CONST_S32 0x100

/* Parse target specific constraints. */
static const char *target_parse_constraint(TCGArgConstraint *ct,
                                           const char *ct_str, TCGType type)
{
    switch (*ct_str++) {
    case 'r':
        ct->ct |= TCG_CT_REG;
        ct->u.regs = 0xff;
        break;
    case 'e':
        ct->ct |= TCG_CT_CONST_S32;
        break;
    default:
        return NULL;
    }
    return ct_str;
}

/* Test if a constant matches the constraint. */
static int tcg_target_const_match(tcg_target_long val, TCGType type,
                                  const TCGArgConstraint *arg_ct)
{
    int ct = arg_ct->ct;
    if (ct & TCG_CT_CONST) {
        return 1;
    }
    if ((ct & TCG_CT_CONST_S32) && val == (int32_t)val) {
        return 1;
    }
    return 0;
}

static void tcg_fmt_rwxy(TCGContext *s, TCIOp opc, TCGReg r, TCGReg w,
                         tcg_target_long x, tcg_target_long y,
                         bool xc, bool yc)
{
    tcg_insn_unit *start = s->code_ptr;
    tcg_insn_unit insn;

    s->code_ptr += 1;

    tcg_debug_assert((unsigned)opc < TCI_NUM_OPC);
    insn = opc << POS_OP;

    tcg_debug_assert((unsigned)r < 8);
    insn |= r << POS_R;

    tcg_debug_assert((unsigned)w < 8);
    insn |= w << POS_W;

    if (xc) {
        tcg_debug_assert(x == (int32_t)x);
        if (x >= MIN_X && x <= MAX_X) {
            x += BIAS_X;
        } else {
            tcg_out32(s, x);
            x = 8;
        }
    } else {
        tcg_debug_assert(x >= 0 && x < 8);
    }
    insn |= x << POS_X;

    if (yc) {
        tcg_debug_assert(y == (int32_t)y);
        if (y >= MIN_Y && y <= MAX_Y) {
            y += BIAS_Y;
        } else {
            tcg_out32(s, y);
            y = 8;
        }
    } else {
        tcg_debug_assert(y >= 0 && y < 8);
    }
    insn |= y << POS_Y;

    *start = insn;
}

static inline void tcg_fmt_rxy(TCGContext *s, TCIOp opc, TCGReg r,
                               TCGArg x, TCGArg y, bool xc, bool yc)
{
    tcg_fmt_rwxy(s, opc, r, 0, x, y, xc, yc);
}

static inline void tcg_fmt_r(TCGContext *s, TCIOp opc, TCGReg r)
{
    tcg_fmt_rwxy(s, opc, r, 0, 0, 0, 1, 1);
}

static inline void tcg_fmt_xy(TCGContext *s, TCIOp opc, TCGArg x, TCGArg y,
                              bool xc, bool yc)
{
    tcg_fmt_rwxy(s, opc, 0, 0, x, y, xc, yc);
}

static inline void tcg_fmt_x(TCGContext *s, TCIOp opc, TCGArg x, bool xc)
{
    tcg_fmt_rwxy(s, opc, 0, 0, x, 0, xc, 1);
}

static inline void tcg_fmt_ptr(TCGContext *s, TCIOp opc, uintptr_t p)
{
    /* Set R appropriate for calls and W appropriate for 32-bit call8.
       The values are ignored in all other cases.  */
    tcg_fmt_rwxy(s, opc, TCG_REG_A, TCG_REG_B,
                 (int32_t)(p >> 31 >> 1), (int32_t)p, 1, 1);
}

static inline void tcg_fmt_extract(TCGContext *s, TCIOp opc, TCGReg r,
                                   TCGReg x, int pos, int len)
{
    int poslen = pos << 6 | len;
    tcg_fmt_rxy(s, opc, r, x, poslen, 0, 1);
}

static inline void tcg_out_mov(TCGContext *s, TCGType type,
                               TCGReg ret, TCGReg val)
{
    if (ret != val) {
        tcg_fmt_rxy(s, TCI_ior, ret, 0, val, 1, 0);
    }
}

static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long val)
{
    if (type == TCG_TYPE_I32 || val == (int32_t)val) {
        tcg_fmt_rxy(s, TCI_ior, ret, 0, (int32_t)val, 1, 1);
    } else {
        tcg_fmt_rxy(s, TCI_concat4, ret, val >> 31 >> 1, (int32_t)val, 1, 1);
    }
}

static void tcg_out_br(TCGContext *s, TCIOp opc, TCGLabel *label)
{
    if (label->has_value) {
        intptr_t disp = label->u.value_ptr - (s->code_ptr + 1);
        tcg_debug_assert(disp >= MIN_Y && disp < MAX_Y);
        tcg_fmt_rwxy(s, opc, 0, 0, 0, disp, 1, 1);
    } else {
        /* Retain the current contents of Y during retranslation.  */
        QEMU_BUILD_BUG_ON(POS_Y != 0);
        tcg_out_reloc(s, s->code_ptr, 0, label, 0);
        tcg_out32(s, deposit32(opc << POS_OP, 0, LEN_Y, *s->code_ptr));
    }
}

static void tcg_out_ld(TCGContext *s, TCGType type, TCGReg ret,
                       TCGReg base, intptr_t ofs)
{
    tcg_fmt_rwxy(s, type == TCG_TYPE_I32 ? TCI_ld4u : TCI_ld8,
                 ret, base, 0, ofs, 1, 1);
}

static void tcg_out_st(TCGContext *s, TCGType type, TCGReg val,
                       TCGReg base, intptr_t ofs)
{
    tcg_fmt_rwxy(s, type == TCG_TYPE_I32 ? TCI_st4 : TCI_st8,
                 0, base, val, ofs, 0, 1);
}

static bool tcg_out_sti(TCGContext *s, TCGType type, TCGArg val,
                        TCGReg base, intptr_t ofs)
{
    if (type == TCG_TYPE_I32) {
        val = (int32_t)val;
    } else if (val != (int32_t)val) {
        return false;
    }
    tcg_fmt_rwxy(s, type == TCG_TYPE_I32 ? TCI_st4 : TCI_st8,
                 0, base, val, ofs, 1, 1);
    return true;
}

static const TCIOp tci_cond4[16] = {
    [TCG_COND_EQ] = TCI_cmp4eq,
    [TCG_COND_NE] = TCI_cmp4ne,
    [TCG_COND_LT] = TCI_cmp4lt,
    [TCG_COND_LE] = TCI_cmp4le,
    [TCG_COND_GT] = TCI_cmp4gt,
    [TCG_COND_GE] = TCI_cmp4ge,
    [TCG_COND_LTU] = TCI_cmp4ltu,
    [TCG_COND_LEU] = TCI_cmp4leu,
    [TCG_COND_GTU] = TCI_cmp4gtu,
    [TCG_COND_GEU] = TCI_cmp4geu,
};

static const TCIOp tci_cond8[16] = {
    [TCG_COND_EQ] = TCI_cmp8eq,
    [TCG_COND_NE] = TCI_cmp8ne,
    [TCG_COND_LT] = TCI_cmp8lt,
    [TCG_COND_LE] = TCI_cmp8le,
    [TCG_COND_GT] = TCI_cmp8gt,
    [TCG_COND_GE] = TCI_cmp8ge,
    [TCG_COND_LTU] = TCI_cmp8ltu,
    [TCG_COND_LEU] = TCI_cmp8leu,
    [TCG_COND_GTU] = TCI_cmp8gtu,
    [TCG_COND_GEU] = TCI_cmp8geu,
};

static const TCIOp tci_condp[16] = {
    [TCG_COND_EQ] = TCI_cmppeq,
    [TCG_COND_NE] = TCI_cmppne,
    [TCG_COND_LT] = TCI_cmpplt,
    [TCG_COND_LE] = TCI_cmpple,
    [TCG_COND_GT] = TCI_cmppgt,
    [TCG_COND_GE] = TCI_cmppge,
    [TCG_COND_LTU] = TCI_cmppltu,
    [TCG_COND_LEU] = TCI_cmppleu,
    [TCG_COND_GTU] = TCI_cmppgtu,
    [TCG_COND_GEU] = TCI_cmppgeu,
};

static const TCIOp tci_qemu_ld[16] = {
    [MO_UB]    = TCI_qld1u,
    [MO_SB]    = TCI_qld1s,
    [MO_LEUW]  = TCI_qld2u_le,
    [MO_LESW]  = TCI_qld2s_le,
    [MO_LEUL]  = TCI_qld4u_le,
    [MO_LESL]  = TCI_qld4s_le,
    [MO_LEQ]   = TCI_qld8_le,
    [MO_BEUW]  = TCI_qld2u_be,
    [MO_BESW]  = TCI_qld2s_be,
    [MO_BEUL]  = TCI_qld4u_be,
    [MO_BESL]  = TCI_qld4s_be,
    [MO_BEQ]   = TCI_qld8_be,
};

static const TCIOp tci_qemu_st[16] = {
    [MO_UB]    = TCI_qst1,
    [MO_LEUW]  = TCI_qst2_le,
    [MO_LEUL]  = TCI_qst4_le,
    [MO_LEQ]   = TCI_qst8_le,
    [MO_BEUW]  = TCI_qst2_be,
    [MO_BEUL]  = TCI_qst4_be,
    [MO_BEQ]   = TCI_qst8_be,
};

static const TCIOp tci_operation[NB_OPS] = {
    [INDEX_op_ld8u_i32]     = TCI_ld1u,
    [INDEX_op_ld8u_i64]     = TCI_ld1u,
    [INDEX_op_ld8s_i32]     = TCI_ld1s,
    [INDEX_op_ld8s_i64]     = TCI_ld1s,
    [INDEX_op_ld16u_i32]    = TCI_ld2u,
    [INDEX_op_ld16u_i64]    = TCI_ld2u,
    [INDEX_op_ld16s_i32]    = TCI_ld2s,
    [INDEX_op_ld16s_i64]    = TCI_ld2s,
    [INDEX_op_ld32u_i64]    = TCI_ld4u,
    [INDEX_op_ld32s_i64]    = TCI_ld4s,
    [INDEX_op_ld_i32]       = TCI_ld4u,
    [INDEX_op_ld_i64]       = TCI_ld8,

    [INDEX_op_st8_i32]      = TCI_st1,
    [INDEX_op_st8_i64]      = TCI_st1,
    [INDEX_op_st16_i32]     = TCI_st2,
    [INDEX_op_st16_i64]     = TCI_st2,
    [INDEX_op_st32_i64]     = TCI_st4,
    [INDEX_op_st_i32]       = TCI_st4,
    [INDEX_op_st_i64]       = TCI_st8,

    [INDEX_op_add_i32]      = TCI_add,
    [INDEX_op_add_i64]      = TCI_add,
    [INDEX_op_sub_i32]      = TCI_sub,
    [INDEX_op_sub_i64]      = TCI_sub,
    [INDEX_op_mul_i32]      = TCI_mul,
    [INDEX_op_mul_i64]      = TCI_mul,
    [INDEX_op_div_i32]      = TCI_divs,
    [INDEX_op_div_i64]      = TCI_divs,
    [INDEX_op_rem_i32]      = TCI_rems,
    [INDEX_op_rem_i64]      = TCI_rems,
    [INDEX_op_divu_i32]     = TCI_divu,
    [INDEX_op_divu_i64]     = TCI_divu,
    [INDEX_op_divu_i64]     = TCI_divu,
    [INDEX_op_remu_i32]     = TCI_remu,
    [INDEX_op_remu_i64]     = TCI_remu,
    [INDEX_op_and_i32]      = TCI_and,
    [INDEX_op_and_i64]      = TCI_and,
    [INDEX_op_or_i32]       = TCI_ior,
    [INDEX_op_or_i64]       = TCI_ior,
    [INDEX_op_xor_i32]      = TCI_xor,
    [INDEX_op_xor_i64]      = TCI_xor,
    [INDEX_op_andc_i32]     = TCI_andc,
    [INDEX_op_andc_i64]     = TCI_andc,
    [INDEX_op_orc_i32]      = TCI_iorc,
    [INDEX_op_orc_i64]      = TCI_iorc,
    [INDEX_op_eqv_i32]      = TCI_xorc,
    [INDEX_op_eqv_i64]      = TCI_xorc,
    [INDEX_op_nand_i32]     = TCI_nand,
    [INDEX_op_nand_i64]     = TCI_nand,
    [INDEX_op_nor_i32]      = TCI_nior,
    [INDEX_op_nor_i64]      = TCI_nior,
    [INDEX_op_shl_i32]      = TCI_shl,
    [INDEX_op_shl_i64]      = TCI_shl,
    [INDEX_op_sar_i32]      = TCI_sar4,
    [INDEX_op_sar_i64]      = TCI_sar8,
    [INDEX_op_shr_i32]      = TCI_shr4,
    [INDEX_op_shr_i64]      = TCI_shr8,
    [INDEX_op_rotl_i32]     = TCI_rol4,
    [INDEX_op_rotr_i32]     = TCI_ror4,
    [INDEX_op_rotl_i64]     = TCI_rol8,
    [INDEX_op_rotr_i64]     = TCI_ror8,

    [INDEX_op_bswap16_i32]  = TCI_bswap2,
    [INDEX_op_bswap16_i64]  = TCI_bswap2,
    [INDEX_op_bswap32_i32]  = TCI_bswap4,
    [INDEX_op_bswap32_i64]  = TCI_bswap4,
    [INDEX_op_bswap64_i64]  = TCI_bswap8,

    [INDEX_op_setcond_i32]  = TCI_setc,
    [INDEX_op_setcond_i64]  = TCI_setc,
    [INDEX_op_movcond_i32]  = TCI_movc,
    [INDEX_op_movcond_i64]  = TCI_movc,

#if TCG_TARGET_REG_BITS == 64
    [INDEX_op_add2_i64]     = TCI_add2,
    [INDEX_op_sub2_i64]     = TCI_sub2,
    [INDEX_op_mulu2_i64]    = TCI_mulu2,
    [INDEX_op_muls2_i64]    = TCI_muls2,
    [INDEX_op_clz_i64]      = TCI_clz,
    [INDEX_op_ctz_i64]      = TCI_ctz,
    [INDEX_op_ctpop_i64]    = TCI_ctpop,
#else
    [INDEX_op_add2_i32]     = TCI_add2,
    [INDEX_op_sub2_i32]     = TCI_sub2,
    [INDEX_op_mulu2_i32]    = TCI_mulu2,
    [INDEX_op_muls2_i32]    = TCI_muls2,
    [INDEX_op_clz_i32]      = TCI_clz,
    [INDEX_op_ctz_i32]      = TCI_ctz,
    [INDEX_op_ctpop_i32]    = TCI_ctpop,
#endif
};

static void tcg_out_call(TCGContext *s, tcg_insn_unit *arg)
{
    const TCGHelperInfo *info;
    TCIOp op;

    info = g_hash_table_lookup(s->helpers, (gpointer)arg);
    if (info->cif->rtype == &ffi_type_void) {
        op = TCI_call0;
    } else if (info->cif->rtype->size == 4) {
        op = TCI_call4;
    } else {
        tcg_debug_assert(info->cif->rtype->size == 8);
        op = TCI_call8;
    }

    tcg_fmt_ptr(s, op, (uintptr_t)info);
}

static void tcg_out_op(TCGContext *s, TCGOpcode tcg_opc, const TCGArg *args,
                       const int *const_args)
{
    TCIOp tci_opc = tci_operation[tcg_opc];
    TCGArg a0, a1, a2;
    TCGMemOpIdx oi;
    TCGMemOp mo;
    int c1, c2, mi, pos, len, poslen;

    a0 = args[0];
    a1 = args[1];
    a2 = args[2];
    c1 = const_args[1];
    c2 = const_args[2];

    switch (tcg_opc) {
    case INDEX_op_exit_tb:
        tcg_fmt_ptr(s, TCI_exit, a0);
        break;

    case INDEX_op_goto_tb:
        /* Indirect jump method. */
        tcg_debug_assert(s->tb_jmp_insn_offset == 0);
        tcg_fmt_ptr(s, TCI_goto_tb, (uintptr_t)(s->tb_jmp_target_addr + a0));
        s->tb_jmp_reset_offset[a0] = tcg_current_code_size(s);
        break;

    case INDEX_op_goto_ptr:
        tcg_fmt_xy(s, TCI_goto_ptr, 0, a0, 1, 0);
        break;

    case INDEX_op_br:
        tcg_out_br(s, TCI_b, arg_label(a0));
        break;

    case INDEX_op_st8_i32:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i32:
    case INDEX_op_st16_i64:
    case INDEX_op_st_i32:
    case INDEX_op_st32_i64:
        a0 = (int32_t)a0;
        /* fall through */
    case INDEX_op_st_i64:
        tcg_fmt_rwxy(s, tci_opc, 0, a1, a0, (intptr_t)a2, const_args[0], 1);
        break;
    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld_i32:
    case INDEX_op_ld32s_i64:
    case INDEX_op_ld_i64:
        tcg_fmt_rwxy(s, tci_opc, a0, a1, 0, (intptr_t)a2, 1, 1);
        break;

    case INDEX_op_deposit_i32:
        a2 = (int32_t)a2;
    case INDEX_op_deposit_i64:
        pos = args[3], len = args[4];
        if (pos == 32 && len == 32) {
            tcg_fmt_rxy(s, TCI_concat4, a0, a2, a1, c2, 0);
            break;
        } else if (c2 && a2 == 0) {
            tcg_target_long mask = deposit64(-1, pos, len, 0);
            if (mask >= MIN_Y && mask <= MAX_Y) {
                tcg_fmt_rxy(s, TCI_and, a0, a1, mask, 0, 1);
                break;
            }
        }
        poslen = (pos << 6) | len;
        tcg_fmt_rwxy(s, TCI_deposit, a0, a1, a2, poslen, c2, 1);
        break;

    case INDEX_op_extract_i32:
    case INDEX_op_extract_i64:
        pos = a2, len = args[3];
        if (pos == 0) {
            tcg_target_long mask = extract64(-1, 0, len);
            if (mask >= MIN_Y && mask <= MAX_Y) {
                tcg_fmt_rxy(s, TCI_and, a0, a1, mask, 0, 1);
                break;
            }
        }
        tcg_fmt_extract(s, TCI_extract, a0, a1, a2, args[3]);
        break;

    case INDEX_op_sextract_i32:
    case INDEX_op_sextract_i64:
        tcg_fmt_extract(s, TCI_sextract, a0, a1, a2, args[3]);
        break;

    case INDEX_op_ext8u_i32:
    case INDEX_op_ext8u_i64:
        tcg_fmt_rxy(s, TCI_and, a0, a1, 0xff, 0, 1);
        break;
    case INDEX_op_ext16u_i32:
    case INDEX_op_ext16u_i64:
        tcg_fmt_extract(s, TCI_extract, a0, a1, 0, 16);
        break;
    case INDEX_op_ext32u_i64:
    case INDEX_op_extu_i32_i64:
        tcg_fmt_rxy(s, TCI_concat4, a0, 0, a1, 1, 0);
        break;
    case INDEX_op_ext8s_i32:
    case INDEX_op_ext8s_i64:
        tcg_fmt_extract(s, TCI_sextract, a0, a1, 0, 8);
        break;
    case INDEX_op_ext16s_i32:
    case INDEX_op_ext16s_i64:
        tcg_fmt_extract(s, TCI_sextract, a0, a1, 0, 16);
        break;
    case INDEX_op_ext32s_i64:
    case INDEX_op_ext_i32_i64:
        tcg_fmt_extract(s, TCI_sextract, a0, a1, 0, 32);
        break;

    case INDEX_op_setcond_i32:
        tci_opc = tci_cond4[args[3]];
        a1 = (int32_t)a1;
        a2 = (int32_t)a2;
        goto do_setcond;
    case INDEX_op_setcond_i64:
        tci_opc = tci_cond8[args[3]];
    do_setcond:
        tcg_fmt_xy(s, tci_opc, a1, a2, c1, c2);
        tcg_fmt_r(s, TCI_setc, a0);
        break;

    case INDEX_op_brcond_i32:
        tci_opc = tci_cond4[a2];
        a0 = (int32_t)a0;
        a1 = (int32_t)a1;
        goto do_brcond;
    case INDEX_op_brcond_i64:
        tci_opc = tci_cond8[a2];
    do_brcond:
        tcg_fmt_xy(s, tci_opc, a0, a1, const_args[0], c1);
        tcg_out_br(s, TCI_bc, arg_label(args[3]));
        break;

    case INDEX_op_movcond_i32:
        tci_opc = tci_cond4[args[5]];
        tcg_fmt_xy(s, tci_opc, (int32_t)a1, (int32_t)a2, c1, c2);
        tcg_fmt_rxy(s, TCI_movc, a0, (int32_t)args[3], (int32_t)args[4],
                    const_args[3], const_args[4]);
        break;
    case INDEX_op_movcond_i64:
        tci_opc = tci_cond8[args[5]];
        tcg_fmt_xy(s, tci_opc, a1, a2, c1, c2);
        tcg_fmt_rxy(s, TCI_movc, a0, args[3], args[4],
                    const_args[3], const_args[4]);
        break;

    case INDEX_op_qemu_ld_i64:
        if (TCG_TARGET_REG_BITS == 32) {
            oi = args[3];
            mo = get_memop(oi);
            mi = get_mmuidx(oi);
            tci_opc = tci_qemu_ld[mo & (MO_BSWAP | MO_SSIZE)];
            tcg_fmt_rwxy(s, tci_opc, a0, a1, a2, get_mmuidx(oi), c2, 1);
            break;
        }
        /* fall through */
    case INDEX_op_qemu_ld_i32:
        oi = a2;
        mo = get_memop(oi);
        mi = get_mmuidx(oi);
        tci_opc = tci_qemu_ld[mo & (MO_BSWAP | MO_SSIZE)];
        tcg_fmt_rwxy(s, tci_opc, a0, 0, a1, mi, c1, 1);
        break;

    case INDEX_op_qemu_st_i64:
        if (TCG_TARGET_REG_BITS == 32) {
            oi = args[3];
            mo = get_memop(oi);
            mi = get_mmuidx(oi);
            tci_opc = tci_qemu_st[mo & (MO_BSWAP | MO_SSIZE)];
            tcg_fmt_rwxy(s, tci_opc, a1, a2, a0, mi, const_args[0], 1);
        } else {
            oi = a2;
            mo = get_memop(oi);
            mi = get_mmuidx(oi);
            tci_opc = tci_qemu_st[mo & (MO_BSWAP | MO_SSIZE)];
            tcg_fmt_rwxy(s, tci_opc, 0, a1, a0, mi, const_args[0], 1);
        }
        break;
    case INDEX_op_qemu_st_i32:
        oi = a2;
        mo = get_memop(oi);
        mi = get_mmuidx(oi);
        tci_opc = tci_qemu_st[mo & (MO_BSWAP | MO_SSIZE)];
        tcg_fmt_rwxy(s, tci_opc, 0, a1, (int32_t)a0, mi, const_args[0], 1);
        break;

    case INDEX_op_add2_i32:
    case INDEX_op_sub2_i32:
    case INDEX_op_add2_i64:
    case INDEX_op_sub2_i64:
        tcg_fmt_rwxy(s, tci_opc, a0, a1, args[5], args[4],
                     const_args[5], const_args[4]);
        break;

    case INDEX_op_mulu2_i32:
    case INDEX_op_muls2_i32:
    case INDEX_op_mulu2_i64:
    case INDEX_op_muls2_i64:
        tcg_fmt_rwxy(s, tci_opc, a0, a1, a2, args[3], c2, const_args[3]);
        break;

    case INDEX_op_setcond2_i32:
        tci_opc = tci_condp[args[5]];
        tcg_fmt_rwxy(s, tci_opc, a1, a2, args[4], args[3],
                     const_args[4], const_args[3]);
        tcg_fmt_r(s, TCI_setc, a0);
        break;
    case INDEX_op_brcond2_i32:
        tci_opc = tci_condp[args[4]];
        tcg_fmt_rwxy(s, tci_opc, a0, a1, args[3], a2, const_args[3], c2);
        tcg_out_br(s, TCI_bc, arg_label(args[5]));
        break;
    case INDEX_op_mb:
        tcg_fmt_r(s, TCI_mb, 0);
        break;

    default:
        if ((tcg_op_defs[tcg_opc].flags & TCG_OPF_64BIT) == 0) {
            a1 = (int32_t)a1;
            a2 = (int32_t)a2;
        }
        tcg_debug_assert(tci_opc != TCI_invalid);
        if (tci_opc <= TCI_LAST_BINARY_OPC) {
            tcg_fmt_rxy(s, tci_opc, a0, a1, a2, c1, c2);
        } else {
            tcg_debug_assert(tci_opc <= TCI_LAST_UNARY_OPC);
            tcg_fmt_rxy(s, tci_opc, a0, 0, a1, 1, c1);
        }
        break;
    case INDEX_op_mov_i32:  /* Always emitted via tcg_out_mov.  */
    case INDEX_op_mov_i64:
    case INDEX_op_movi_i32: /* Always emitted via tcg_out_movi.  */
    case INDEX_op_movi_i64:
    case INDEX_op_call:     /* Always emitted via tcg_out_call.  */
        tcg_abort();
    }
}

/* Generate global QEMU prologue and epilogue code. */
static inline void tcg_target_qemu_prologue(TCGContext *s)
{
}

static const TCGTargetOpDef *tcg_target_op_def(TCGOpcode op)
{
    static const TCGTargetOpDef r = { .args_ct_str = { "r" } };
    static const TCGTargetOpDef r_r = { .args_ct_str = { "r", "r" } };
    static const TCGTargetOpDef re_r = { .args_ct_str = { "re", "r" } };
    static const TCGTargetOpDef re_re = { .args_ct_str = { "re", "re" } };
    static const TCGTargetOpDef r_r_re = { .args_ct_str = { "r", "r", "re" } };
    static const TCGTargetOpDef r_re_re
        = { .args_ct_str = { "r", "re", "re" } };
    static const TCGTargetOpDef r_r_r_r
        = { .args_ct_str = { "r", "r", "r", "r" } };
    static const TCGTargetOpDef ri_r_r_r
        = { .args_ct_str = { "ri", "r", "r", "r" } };
    static const TCGTargetOpDef r_r_re_re
        = { .args_ct_str = { "r", "r", "re", "re" } };

    switch (op) {
    case INDEX_op_goto_ptr:
        return &r;

    case INDEX_op_ld8u_i32:
    case INDEX_op_ld8s_i32:
    case INDEX_op_ld16u_i32:
    case INDEX_op_ld16s_i32:
    case INDEX_op_ld_i32:
    case INDEX_op_ext8s_i32:
    case INDEX_op_ext8u_i32:
    case INDEX_op_ext16s_i32:
    case INDEX_op_ext16u_i32:
    case INDEX_op_bswap16_i32:
    case INDEX_op_bswap32_i32:
    case INDEX_op_extract_i32:
    case INDEX_op_sextract_i32:
    case INDEX_op_qemu_ld_i32:
    case INDEX_op_ld8u_i64:
    case INDEX_op_ld8s_i64:
    case INDEX_op_ld16u_i64:
    case INDEX_op_ld16s_i64:
    case INDEX_op_ld32u_i64:
    case INDEX_op_ld32s_i64:
    case INDEX_op_ld_i64:
    case INDEX_op_ext8s_i64:
    case INDEX_op_ext8u_i64:
    case INDEX_op_ext16s_i64:
    case INDEX_op_ext16u_i64:
    case INDEX_op_ext32s_i64:
    case INDEX_op_ext32u_i64:
    case INDEX_op_bswap16_i64:
    case INDEX_op_bswap32_i64:
    case INDEX_op_bswap64_i64:
    case INDEX_op_ext_i32_i64:
    case INDEX_op_extu_i32_i64:
    case INDEX_op_extract_i64:
    case INDEX_op_sextract_i64:
    case INDEX_op_ctpop_i32:
    case INDEX_op_ctpop_i64:
        return &r_r;

    case INDEX_op_st8_i32:
    case INDEX_op_st16_i32:
    case INDEX_op_st_i32:
    case INDEX_op_qemu_st_i32:
    case INDEX_op_st8_i64:
    case INDEX_op_st16_i64:
    case INDEX_op_st32_i64:
    case INDEX_op_st_i64:
        return &re_r;

    case INDEX_op_add_i32:
    case INDEX_op_mul_i32:
    case INDEX_op_and_i32:
    case INDEX_op_or_i32:
    case INDEX_op_xor_i32:
    case INDEX_op_nand_i32:
    case INDEX_op_nor_i32:
    case INDEX_op_eqv_i32:
    case INDEX_op_deposit_i32:
    case INDEX_op_add_i64:
    case INDEX_op_mul_i64:
    case INDEX_op_and_i64:
    case INDEX_op_or_i64:
    case INDEX_op_xor_i64:
    case INDEX_op_nand_i64:
    case INDEX_op_nor_i64:
    case INDEX_op_eqv_i64:
    case INDEX_op_deposit_i64:
    case INDEX_op_clz_i32:
    case INDEX_op_clz_i64:
    case INDEX_op_ctz_i32:
    case INDEX_op_ctz_i64:
        return &r_r_re;

    case INDEX_op_sub_i32:
    case INDEX_op_div_i32:
    case INDEX_op_rem_i32:
    case INDEX_op_divu_i32:
    case INDEX_op_remu_i32:
    case INDEX_op_andc_i32:
    case INDEX_op_orc_i32:
    case INDEX_op_shl_i32:
    case INDEX_op_shr_i32:
    case INDEX_op_sar_i32:
    case INDEX_op_rotl_i32:
    case INDEX_op_rotr_i32:
    case INDEX_op_setcond_i32:
    case INDEX_op_sub_i64:
    case INDEX_op_div_i64:
    case INDEX_op_rem_i64:
    case INDEX_op_divu_i64:
    case INDEX_op_remu_i64:
    case INDEX_op_andc_i64:
    case INDEX_op_orc_i64:
    case INDEX_op_shl_i64:
    case INDEX_op_shr_i64:
    case INDEX_op_sar_i64:
    case INDEX_op_rotl_i64:
    case INDEX_op_rotr_i64:
    case INDEX_op_setcond_i64:
        return &r_re_re;

    case INDEX_op_brcond_i32:
    case INDEX_op_brcond_i64:
        return &re_re;

    case INDEX_op_qemu_ld_i64:
        return TCG_TARGET_REG_BITS == 64 ? &r_r : &r_r_r_r;
    case INDEX_op_qemu_st_i64:
        return TCG_TARGET_REG_BITS == 64 ? &re_r : &ri_r_r_r;

    case INDEX_op_mulu2_i32:
    case INDEX_op_muls2_i32:
    case INDEX_op_mulu2_i64:
    case INDEX_op_muls2_i64:
    case INDEX_op_brcond2_i32:
        return &r_r_re_re;

    case INDEX_op_movcond_i32:
    case INDEX_op_movcond_i64:
        {
            static const TCGTargetOpDef movc
                = { .args_ct_str = { "r", "re", "re", "re", "re" } };
            return &movc;
        }

    case INDEX_op_add2_i32:
    case INDEX_op_add2_i64:
    case INDEX_op_sub2_i32:
    case INDEX_op_sub2_i64:
        {
            static const TCGTargetOpDef as2
                = { .args_ct_str = { "r", "r", "0", "1", "re", "re" } };
            return &as2;
        }

    case INDEX_op_setcond2_i32:
        {
            static const TCGTargetOpDef sc2
                = { .args_ct_str = { "r", "r", "r", "re", "re" } };
            return &sc2;
        }

    default:
        return NULL;
    }
}

static void tcg_target_init(TCGContext *s)
{
    tcg_target_available_regs[TCG_TYPE_I32] = 0xff;
    if (TCG_TARGET_REG_BITS == 64) {
        tcg_target_available_regs[TCG_TYPE_I64] = 0xff;
    }

    tcg_target_call_clobber_regs = 0;
    tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_A);
    if (TCG_TARGET_REG_BITS == 32) {
        tcg_regset_set_reg(tcg_target_call_clobber_regs, TCG_REG_B);
    }

    s->reserved_regs = 0;
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_SP);

    tcg_set_frame(s, TCG_REG_SP, 0, TCG_STATIC_CALL_ARGS_SIZE);
}

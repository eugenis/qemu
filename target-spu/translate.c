/*
 *  Synergistic Processor Unit (SPU) emulation
 *  CPU Translation.
 *
 *  Copyright (c) 2011  Richard Henderson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "disas/disas.h"
#include "tcg-op.h"
#include "qemu/host-utils.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"


typedef struct {
    struct TranslationBlock *tb;
    uint32_t pc;
    uint32_t lslr;
    bool singlestep;
} DisassContext;

/* Return values from translate_one, indicating the state of the TB.
   Note that zero indicates that we are not exiting the TB.  */
typedef enum {
    NO_INSN,
    NO_EXIT,

    /* We have emitted one or more goto_tb.  No fixup required.  */
    EXIT_GOTO_TB,

    /* We are not using a goto_tb (for whatever reason), but have updated
       the PC (for whatever reason), so there's no need to do it again on
       exiting the TB.  */
    EXIT_PC_UPDATED,

    /* We are exiting the TB, but have neither emitted a goto_tb, nor
       updated the PC for the next instruction to be executed.  */
    EXIT_PC_STALE,

    /* We are ending the TB with a noreturn function call, e.g. longjmp.
       No following code will be executed.  */
    EXIT_NORETURN,
} ExitStatus;

/* Global registers.  */
static TCGv_ptr cpu_env;
static TCGv cpu_pc;
static TCGv cpu_srr0;
static TCGv cpu_gpr[128][4];

/* register names */
static char cpu_reg_names[128][4][8];

#include "exec/gen-icount.h"

/* To be used in "insn_foo", return "foo".  */
#define INSN  (__FUNCTION__ + 5)

#define DISASS_RR                               \
    unsigned rt = extract32(insn, 0, 7);        \
    unsigned ra = extract32(insn, 7, 7);        \
    unsigned rb = extract32(insn, 14, 7);       \
    qemu_log_mask(CPU_LOG_TB_IN_ASM, "%8x:\t%s\t$%d,$%d,$%d\n", \
                  ctx->pc, INSN, rt, ra, rb)

#define DISASS_RR1                              \
    unsigned rt = extract32(insn, 0, 7);        \
    unsigned ra = extract32(insn, 7, 7);        \
    qemu_log_mask(CPU_LOG_TB_IN_ASM, "%8x:\t%s\t$%d,$%d\n", \
                  ctx->pc, INSN, rt, ra)

#define DISASS_RR_BIRR                          \
    unsigned rt = extract32(insn, 0, 7);        \
    unsigned ra = extract32(insn, 7, 7);        \
    unsigned rb = extract32(insn, 14, 7);       \
    int enadis = (rb & 0x20 ? -1 : rb & 0x10 ? 1 : 0); \
    qemu_log_mask(CPU_LOG_TB_IN_ASM, "%8x:\t%s%s\t$%d,$%d\n", \
                  ctx->pc, INSN, (enadis < 0 ? "d" : enadis > 0 ? "e" : ""), \
                  rt, ra)

#define DISASS_RR_BIR                           \
    unsigned ra = extract32(insn, 7, 7);        \
    unsigned rb = extract32(insn, 14, 7);       \
    int enadis = (rb & 0x20 ? -1 : rb & 0x10 ? 1 : 0); \
    qemu_log_mask(CPU_LOG_TB_IN_ASM, "%8x:\t%s%s\t$%d\n", \
                  ctx->pc, INSN, (enadis < 0 ? "d" : enadis > 0 ? "e" : ""), ra)

#define DISASS_RRR                              \
    unsigned rt = extract32(insn, 21, 7);       \
    unsigned ra = extract32(insn, 7, 7);        \
    unsigned rb = extract32(insn, 14, 7);       \
    unsigned rc = extract32(insn, 0, 7);        \
    qemu_log_mask(CPU_LOG_TB_IN_ASM, "%8x:\t%s\t$%d,$%d,$%d,$%d\n", \
                  ctx->pc, INSN, rt, ra, rb, rc)

#define DISASS_RI7                              \
    unsigned rt = extract32(insn, 0, 7);        \
    unsigned ra = extract32(insn, 7, 7);        \
    int32_t imm = sextract32(insn, 14, 7);      \
    qemu_log_mask(CPU_LOG_TB_IN_ASM, "%8x:\t%s\t$%d,$%d,%d\n", \
                  ctx->pc, INSN, rt, ra, imm)

#define DISASS_RI10                             \
    unsigned rt = extract32(insn, 0, 7);        \
    unsigned ra = extract32(insn, 7, 7);        \
    int32_t imm = sextract32(insn, 14, 10);     \
    qemu_log_mask(CPU_LOG_TB_IN_ASM, "%8x:\t%s\t$%d,$%d,%d\n", \
                  ctx->pc, INSN, rt, ra, imm)

#define DISASS_RI16                             \
    unsigned rt = extract32(insn, 0, 7);        \
    int32_t imm = sextract32(insn, 7, 16);      \
    qemu_log_mask(CPU_LOG_TB_IN_ASM, "%8x:\t%s\t$%d,%d\n", \
                  ctx->pc, INSN, rt, imm)

#define DISASS_I16                              \
    int32_t imm = sextract32(insn, 7, 16);      \
    qemu_log_mask(CPU_LOG_TB_IN_ASM, "%8x:\t%s\t%d\n", ctx->pc, INSN, imm)

#define DISASS_RI18                             \
    unsigned rt = extract32(insn, 0, 7);        \
    int32_t imm = extract32(insn, 7, 18);       \
    qemu_log_mask(CPU_LOG_TB_IN_ASM, "%8x:\t%s\t$%d,%d\n", \
                  ctx->pc, INSN, rt, imm)


static inline void load_temp_imm(TCGv temp[4], int32_t imm)
{
    temp[0] = tcg_const_tl(imm);
    temp[1] = temp[0];
    temp[2] = temp[0];
    temp[3] = temp[0];
}

static inline void alloc_temp(TCGv temp[4])
{
    int i;
    for (i = 0; i < 4; ++i) {
        temp[i] = tcg_temp_new();
    }
}

static inline void free_temp(TCGv temp[4])
{
    tcg_temp_free(temp[0]);
    if (!TCGV_EQUAL(temp[0], temp[1])) {
        tcg_temp_free(temp[1]);
    }
    if (!TCGV_EQUAL(temp[0], temp[2])) {
        tcg_temp_free(temp[2]);
    }
    if (!TCGV_EQUAL(temp[0], temp[3])) {
        tcg_temp_free(temp[3]);
    }
}

static inline void foreach_op2 (void (*op)(TCGv, TCGv), TCGv rt[4], TCGv ra[4])
{
    int i;
    for (i = 0; i < 4; ++i) {
        op(rt[i], ra[i]);
    }
}

static inline void foreach_op3 (void (*op)(TCGv, TCGv, TCGv),
                                TCGv rt[4], TCGv ra[4], TCGv rb[4])
{
    int i;
    for (i = 0; i < 4; ++i) {
        op(rt[i], ra[i], rb[i]);
    }
}

static inline void foreach_op4 (void (*op)(TCGv, TCGv, TCGv, TCGv), TCGv rt[4],
                                           TCGv ra[4], TCGv rb[4], TCGv rc[4])
{
    int i;
    for (i = 0; i < 4; ++i) {
        op(rt[i], ra[i], rb[i], rc[i]);
    }
}

#define FOREACH_RR(NAME, FN)                                            \
static ExitStatus insn_##NAME(DisassContext *ctx, uint32_t insn)        \
{                                                                       \
    DISASS_RR;                                                          \
    foreach_op3(FN, cpu_gpr[rt], cpu_gpr[ra], cpu_gpr[rb]);             \
    return NO_EXIT;                                                     \
}

#define FOREACH_RR1(NAME, FN)                                           \
static ExitStatus insn_##NAME(DisassContext *ctx, uint32_t insn)        \
{                                                                       \
    DISASS_RR1;                                                         \
    foreach_op2(FN, cpu_gpr[rt], cpu_gpr[ra]);                          \
    return NO_EXIT;                                                     \
}

#define FOREACH_RRR(NAME, FN)                                           \
static ExitStatus insn_##NAME(DisassContext *ctx, uint32_t insn)        \
{                                                                       \
    DISASS_RRR;                                                         \
    foreach_op4(FN, cpu_gpr[rt], cpu_gpr[ra], cpu_gpr[rb], cpu_gpr[rc]);\
    return NO_EXIT;                                                     \
}

#define FOREACH_RI7_ADJ(NAME, FN, ADJUST_IMM)                           \
static ExitStatus insn_##NAME(DisassContext *ctx, uint32_t insn)        \
{                                                                       \
    TCGv temp[4];                                                       \
    DISASS_RI7;                                                         \
    ADJUST_IMM;                                                         \
    load_temp_imm(temp, imm);                                           \
    foreach_op3(FN, cpu_gpr[rt], cpu_gpr[ra], temp);                    \
    free_temp(temp);                                                    \
    return NO_EXIT;                                                     \
}

#define FOREACH_RI7(NAME, FN)  FOREACH_RI7_ADJ(NAME, FN, )

#define FOREACH_RI10_ADJ(NAME, FN, ADJUST_IMM)                          \
static ExitStatus insn_##NAME(DisassContext *ctx, uint32_t insn)        \
{                                                                       \
    TCGv temp[4];                                                       \
    DISASS_RI10;                                                        \
    ADJUST_IMM;                                                         \
    load_temp_imm(temp, imm);                                           \
    foreach_op3(FN, cpu_gpr[rt], cpu_gpr[ra], temp);                    \
    free_temp(temp);                                                    \
    return NO_EXIT;                                                     \
}

#define FOREACH_RI10(NAME, FN)  FOREACH_RI10_ADJ(NAME, FN, )

/* ---------------------------------------------------------------------- */
/* Section 2: Memory Load/Store Instructions.  */

static TCGv gen_address_a(DisassContext *ctx, uint32_t a)
{
    return tcg_const_tl(a & ctx->lslr & ~0xf);
}

static TCGv gen_address_x(DisassContext *ctx, TCGv a, TCGv b)
{
    TCGv addr = tcg_temp_new();
    tcg_gen_add_tl(addr, a, b);
    tcg_gen_andi_tl(addr, addr, ctx->lslr & ~0xf);
    return addr;
}

static TCGv gen_address_d(DisassContext *ctx, TCGv a, int32_t disp)
{
    TCGv addr = tcg_temp_new();
    tcg_gen_andi_tl(addr, a, ctx->lslr & ~0xf);
    tcg_gen_addi_tl(addr, addr, disp * 16);
    return addr;
}

static ExitStatus gen_loadq(TCGv addr, TCGv reg[4])
{
    tcg_gen_qemu_ld32u(reg[0], addr, 0);
    tcg_gen_addi_tl(addr, addr, 4);
    tcg_gen_qemu_ld32u(reg[1], addr, 0);
    tcg_gen_addi_tl(addr, addr, 4);
    tcg_gen_qemu_ld32u(reg[2], addr, 0);
    tcg_gen_addi_tl(addr, addr, 4);
    tcg_gen_qemu_ld32u(reg[3], addr, 0);
    tcg_temp_free(addr);
    return NO_EXIT;
}

static ExitStatus gen_storeq(TCGv addr, TCGv reg[4])
{
    tcg_gen_qemu_st32(reg[0], addr, 0);
    tcg_gen_addi_tl(addr, addr, 4);
    tcg_gen_qemu_st32(reg[1], addr, 0);
    tcg_gen_addi_tl(addr, addr, 4);
    tcg_gen_qemu_st32(reg[2], addr, 0);
    tcg_gen_addi_tl(addr, addr, 4);
    tcg_gen_qemu_st32(reg[3], addr, 0);
    tcg_temp_free(addr);
    return NO_EXIT;
}

static ExitStatus insn_lqd(DisassContext *ctx, uint32_t insn)
{
    DISASS_RI10;
    return gen_loadq(gen_address_d(ctx, cpu_gpr[ra][0], imm), cpu_gpr[rt]);
}

static ExitStatus insn_lqx(DisassContext *ctx, uint32_t insn)
{
    DISASS_RR;
    return gen_loadq(gen_address_x(ctx, cpu_gpr[ra][0], cpu_gpr[rb][0]),
                     cpu_gpr[rt]);
}

static ExitStatus insn_lqa(DisassContext *ctx, uint32_t insn)
{
    DISASS_RI16;
    return gen_loadq(gen_address_a(ctx, imm * 4), cpu_gpr[rt]);
}

static ExitStatus insn_lqr(DisassContext *ctx, uint32_t insn)
{
    DISASS_RI16;
    return gen_loadq(gen_address_a(ctx, ctx->pc + imm * 4), cpu_gpr[rt]);
}

static ExitStatus insn_stqd(DisassContext *ctx, uint32_t insn)
{
    DISASS_RI10;
    return gen_storeq(gen_address_d(ctx, cpu_gpr[ra][0], imm), cpu_gpr[rt]);
}

static ExitStatus insn_stqx(DisassContext *ctx, uint32_t insn)
{
    DISASS_RR;
    return gen_storeq(gen_address_x(ctx, cpu_gpr[ra][0], cpu_gpr[rb][0]),
                      cpu_gpr[rt]);
}

static ExitStatus insn_stqa(DisassContext *ctx, uint32_t insn)
{
    DISASS_RI16;
    return gen_storeq(gen_address_a(ctx, imm * 4), cpu_gpr[rt]);
}

static ExitStatus insn_stqr(DisassContext *ctx, uint32_t insn)
{
    DISASS_RI16;
    return gen_storeq(gen_address_a(ctx, ctx->pc + imm * 4), cpu_gpr[rt]);
}

/* ---------------------------------------------------------------------- */
/* Section 3: Constant Formation Instructions.  */

static ExitStatus gen_movi(TCGv r[4], int32_t imm)
{
    tcg_gen_movi_i32(r[0], imm);
    tcg_gen_movi_i32(r[1], imm);
    tcg_gen_movi_i32(r[2], imm);
    tcg_gen_movi_i32(r[3], imm);
    return NO_EXIT;
}

static ExitStatus insn_ilh(DisassContext *ctx, uint32_t insn)
{
    DISASS_RI16;

    imm &= 0xffff;
    imm |= imm << 16;
    return gen_movi(cpu_gpr[rt], imm);
}

static ExitStatus insn_ilhu(DisassContext *ctx, uint32_t insn)
{
    DISASS_RI16;

    imm <<= 16;
    return gen_movi(cpu_gpr[rt], imm);
}

static ExitStatus insn_il(DisassContext *ctx, uint32_t insn)
{
    DISASS_RI16;
    return gen_movi(cpu_gpr[rt], imm);
}

static ExitStatus insn_ila(DisassContext *ctx, uint32_t insn)
{
    DISASS_RI18;
    return gen_movi(cpu_gpr[rt], imm);
}

static ExitStatus insn_iohl(DisassContext *ctx, uint32_t insn)
{
    TCGv temp[4];
    DISASS_RI16;

    load_temp_imm(temp, imm);
    foreach_op3(tcg_gen_or_tl, cpu_gpr[rt], cpu_gpr[rt], temp);
    free_temp(temp);
    return NO_EXIT;
}

static ExitStatus insn_fsmbi(DisassContext *ctx, uint32_t insn)
{
    DISASS_RI16;

    tcg_gen_movi_tl(cpu_gpr[rt][0], helper_fsmb(imm << 0));
    tcg_gen_movi_tl(cpu_gpr[rt][1], helper_fsmb(imm << 4));
    tcg_gen_movi_tl(cpu_gpr[rt][2], helper_fsmb(imm << 8));
    tcg_gen_movi_tl(cpu_gpr[rt][3], helper_fsmb(imm << 12));
    return NO_EXIT;
}

/* ---------------------------------------------------------------------- */
/* Section 4: Integer and Logical Instructions.  */

static void gen_addh(TCGv out, TCGv a, TCGv b)
{
    TCGv high = tcg_temp_new();
    TCGv low = tcg_temp_new();

    tcg_gen_add_tl(low, a, b);

    /* By zapping low half of A, we guarantee no carry into high
       without having to fiddle B.  That will get done in DEPOSIT.  */
    tcg_gen_andi_tl(high, a, 0xffff0000);
    tcg_gen_add_tl(out, high, b);
    tcg_gen_deposit_tl(out, out, low, 0, 16);

    tcg_temp_free(high);
    tcg_temp_free(low);
}

FOREACH_RR(ah, gen_addh)
FOREACH_RI10(ahi, gen_addh)

FOREACH_RR(a, tcg_gen_add_tl)
FOREACH_RI10(ai, tcg_gen_add_tl)

static void gen_sfh(TCGv out, TCGv a, TCGv b)
{
    TCGv high = tcg_temp_new();
    TCGv low = tcg_temp_new();

    tcg_gen_sub_tl(low, b, a);

    tcg_gen_andi_tl(high, a, 0xffff0000);
    tcg_gen_sub_tl(out, b, high);
    tcg_gen_deposit_tl(out, out, low, 0, 16);

    tcg_temp_free(high);
    tcg_temp_free(low);
}

FOREACH_RR(sfh, gen_sfh)
FOREACH_RI10(sfhi, gen_sfh)

static void gen_sf(TCGv out, TCGv a, TCGv b)
{
    tcg_gen_sub_tl(out, b, a);
}

FOREACH_RR(sf, gen_sf)
FOREACH_RI10(sfi, gen_sf)

static void gen_addx(TCGv out, TCGv a, TCGv b)
{
    TCGv tmp = tcg_temp_new();

    tcg_gen_andi_tl(tmp, out, 1);
    tcg_gen_add_tl(tmp, tmp, a);
    tcg_gen_add_tl(out, tmp, b);
    
    tcg_temp_free(tmp);
}

FOREACH_RR(addx, gen_addx)

static void gen_cg(TCGv out, TCGv a, TCGv b)
{
    TCGv out_low = tcg_temp_new();
    TCGv in_zero = tcg_const_tl(0);

    tcg_gen_add2_tl(out_low, out, a, in_zero, b, in_zero);

    tcg_temp_free(out_low);
    tcg_temp_free(in_zero);
}

FOREACH_RR(cg, gen_cg)

static void gen_cgx(TCGv out, TCGv a, TCGv b)
{
    TCGv out_low = tcg_temp_new();
    TCGv in_zero = tcg_const_tl(0);
    TCGv out_lsb = tcg_temp_new();

    tcg_gen_andi_tl(out_lsb, out, 1);
    tcg_gen_add2_tl(out_low, out, a, in_zero, b, in_zero);
    tcg_gen_add2_tl(out_low, out, out_low, out, out_lsb, in_zero);

    tcg_temp_free(out_low);
    tcg_temp_free(in_zero);
    tcg_temp_free(out_lsb);
}

FOREACH_RR(cgx, gen_cgx)

static void gen_sfx(TCGv out, TCGv a, TCGv b)
{
    TCGv tmp = tcg_temp_new();
    TCGv lsb = tcg_temp_new();

    /* The description in the manual is convoluted, but it's the same as
       the PowerPC (low, ca) = ~ra + rb + ca.  */
    
    tcg_gen_not_tl(tmp, a);
    tcg_gen_andi_tl(lsb, out, 1);
    tcg_gen_add_tl(tmp, tmp, b);
    tcg_gen_add_tl(out, tmp, lsb);

    tcg_temp_free(tmp);
    tcg_temp_free(lsb);
}

FOREACH_RR(sfx, gen_sfx)

static void gen_bg(TCGv out, TCGv a, TCGv b)
{
    tcg_gen_setcond_i32(TCG_COND_GTU, out, a, b);
}

FOREACH_RR(bg, gen_bg)

static void gen_bgx(TCGv out, TCGv a, TCGv b)
{
    TCGv out_low = tcg_temp_new();
    TCGv in_zero = tcg_const_tl(0);
    TCGv out_lsb = tcg_temp_new();

    /* The description in the manual is convoluted, but it's the same as
       the PowerPC (low, ca) = ~ra + rb + ca.  */

    tcg_gen_not_tl(out_low, a);
    tcg_gen_andi_tl(out_lsb, out, 1);
    tcg_gen_add2_tl(out_low, out, out_low, in_zero, b, in_zero);
    tcg_gen_add2_tl(out_low, out, out_low, out, out_lsb, in_zero);

    tcg_temp_free(out_low);
    tcg_temp_free(in_zero);
    tcg_temp_free(out_lsb);
}

FOREACH_RR(bgx, gen_bgx)

static void gen_mpy(TCGv out, TCGv a, TCGv b)
{
    TCGv al = tcg_temp_new();
    TCGv bl = tcg_temp_new();

    tcg_gen_ext16s_tl(al, a);
    tcg_gen_ext16s_tl(bl, b);
    tcg_gen_mul_tl(out, al, bl);

    tcg_temp_free(al);
    tcg_temp_free(bl);
}

FOREACH_RR(mpy, gen_mpy)
FOREACH_RI10(mpyi, gen_mpy)

static void gen_mpyu(TCGv out, TCGv a, TCGv b)
{
    TCGv al = tcg_temp_new();
    TCGv bl = tcg_temp_new();

    tcg_gen_ext16u_tl(al, a);
    tcg_gen_ext16u_tl(bl, b);
    tcg_gen_mul_tl(out, al, bl);

    tcg_temp_free(al);
    tcg_temp_free(bl);
}

FOREACH_RR(mpyu, gen_mpyu)
FOREACH_RI10(mpyui, gen_mpyu)

static void gen_mpya(TCGv out, TCGv a, TCGv b, TCGv c)
{
    TCGv t = tcg_temp_new();
    gen_mpy(t, a, b);
    tcg_gen_add_tl(out, t, c);
}

FOREACH_RRR(mpya, gen_mpya)

static void gen_mpyh(TCGv out, TCGv a, TCGv b)
{
    TCGv ah = tcg_temp_new();
    TCGv bl = tcg_temp_new();

    tcg_gen_sari_tl(ah, a, 16);
    tcg_gen_ext16s_tl(bl, b);
    tcg_gen_mul_tl(out, ah, bl);

    tcg_temp_free(ah);
    tcg_temp_free(bl);
}

FOREACH_RR(mpyh, gen_mpyh)

static void gen_mpys(TCGv out, TCGv a, TCGv b)
{
    TCGv al = tcg_temp_new();
    TCGv bl = tcg_temp_new();

    tcg_gen_ext16s_tl(al, a);
    tcg_gen_ext16s_tl(bl, b);
    tcg_gen_mul_tl(out, al, bl);
    tcg_gen_sari_tl(out, out, 16);

    tcg_temp_free(al);
    tcg_temp_free(bl);
}

FOREACH_RR(mpys, gen_mpys)

static void gen_mpyhh(TCGv out, TCGv a, TCGv b)
{
    TCGv ah = tcg_temp_new();
    TCGv bh = tcg_temp_new();

    tcg_gen_sari_tl(ah, a, 16);
    tcg_gen_sari_tl(bh, b, 16);
    tcg_gen_mul_tl(out, ah, bh);

    tcg_temp_free(ah);
    tcg_temp_free(bh);
}

FOREACH_RR(mpyhh, gen_mpyhh)

static void gen_mpyhha(TCGv out, TCGv a, TCGv b)
{
    TCGv t = tcg_temp_new();

    gen_mpyhh(t, a, b);
    tcg_gen_add_tl(out, out, t);

    tcg_temp_free(t);
}

FOREACH_RR(mpyhha, gen_mpyhha)

static void gen_mpyhhu(TCGv out, TCGv a, TCGv b)
{
    TCGv ah = tcg_temp_new();
    TCGv bh = tcg_temp_new();

    tcg_gen_shri_tl(ah, a, 16);
    tcg_gen_shri_tl(bh, b, 16);
    tcg_gen_mul_tl(out, ah, bh);

    tcg_temp_free(ah);
    tcg_temp_free(bh);
}

FOREACH_RR(mpyhhu, gen_mpyhhu)

static void gen_mpyhhau(TCGv out, TCGv a, TCGv b)
{
    TCGv t = tcg_temp_new();

    gen_mpyhhu(t, a, b);
    tcg_gen_add_tl(out, out, t);

    tcg_temp_free(t);
}

FOREACH_RR(mpyhhau, gen_mpyhhau)

FOREACH_RR1(clz, gen_helper_clz)
FOREACH_RR1(cntb, gen_helper_cntb)

static ExitStatus insn_fsmb(DisassContext *ctx, uint32_t insn)
{
    TCGv temp = tcg_temp_new();
    DISASS_RR1;

    tcg_gen_mov_tl(temp, cpu_gpr[ra][0]);
    gen_helper_fsmb(cpu_gpr[rt][0], temp);
    tcg_gen_shli_tl(temp, temp, 4);
    gen_helper_fsmb(cpu_gpr[rt][1], temp);
    tcg_gen_shli_tl(temp, temp, 4);
    gen_helper_fsmb(cpu_gpr[rt][2], temp);
    tcg_gen_shli_tl(temp, temp, 4);
    gen_helper_fsmb(cpu_gpr[rt][3], temp);

    tcg_temp_free(temp);
    return NO_EXIT;
}

static ExitStatus insn_fsmh(DisassContext *ctx, uint32_t insn)
{
    TCGv temp = tcg_temp_new();
    DISASS_RR1;

    tcg_gen_mov_tl(temp, cpu_gpr[ra][0]);
    gen_helper_fsmh(cpu_gpr[rt][0], temp);
    tcg_gen_shli_tl(temp, temp, 2);
    gen_helper_fsmh(cpu_gpr[rt][1], temp);
    tcg_gen_shli_tl(temp, temp, 2);
    gen_helper_fsmh(cpu_gpr[rt][2], temp);
    tcg_gen_shli_tl(temp, temp, 2);
    gen_helper_fsmh(cpu_gpr[rt][3], temp);

    tcg_temp_free(temp);
    return NO_EXIT;
}

static ExitStatus insn_fsm(DisassContext *ctx, uint32_t insn)
{
    TCGv hold, test;
    int i;
    DISASS_RR1;

    hold = tcg_temp_new();
    test = tcg_temp_new();

    tcg_gen_mov_tl(hold, cpu_gpr[ra][0]);
    for (i = 0; i < 4; ++i) {
        tcg_gen_shli_tl(test, hold, i);
        tcg_gen_sari_tl(cpu_gpr[rt][i], test, 31);
    }

    tcg_temp_free(test);
    tcg_temp_free(hold);
    return NO_EXIT;
}

static ExitStatus insn_gbb(DisassContext *ctx, uint32_t insn)
{
    DISASS_RR1;

    gen_helper_gbb(cpu_gpr[rt][0], cpu_gpr[ra][0], cpu_gpr[ra][1],
                   cpu_gpr[ra][2], cpu_gpr[ra][3]);
    tcg_gen_movi_tl(cpu_gpr[rt][1], 0);
    tcg_gen_movi_tl(cpu_gpr[rt][2], 0);
    tcg_gen_movi_tl(cpu_gpr[rt][3], 0);
    return NO_EXIT;
}

static ExitStatus insn_gbh(DisassContext *ctx, uint32_t insn)
{
    DISASS_RR1;

    gen_helper_gbh(cpu_gpr[rt][0], cpu_gpr[ra][0], cpu_gpr[ra][1],
                   cpu_gpr[ra][2], cpu_gpr[ra][3]);
    tcg_gen_movi_tl(cpu_gpr[rt][1], 0);
    tcg_gen_movi_tl(cpu_gpr[rt][2], 0);
    tcg_gen_movi_tl(cpu_gpr[rt][3], 0);
    return NO_EXIT;
}

static ExitStatus insn_gb(DisassContext *ctx, uint32_t insn)
{
    DISASS_RR1;

    gen_helper_gb(cpu_gpr[rt][0], cpu_gpr[ra][0], cpu_gpr[ra][1],
                  cpu_gpr[ra][2], cpu_gpr[ra][3]);
    tcg_gen_movi_tl(cpu_gpr[rt][1], 0);
    tcg_gen_movi_tl(cpu_gpr[rt][2], 0);
    tcg_gen_movi_tl(cpu_gpr[rt][3], 0);
    return NO_EXIT;
}

FOREACH_RR(avgb, gen_helper_avgb)
FOREACH_RR(absdb, gen_helper_absdb)
FOREACH_RR(sumb, gen_helper_sumb)

static void gen_xsbh(TCGv out, TCGv in)
{
    TCGv temp = tcg_temp_new();

    tcg_gen_ext8s_tl(temp, in);
    tcg_gen_shli_tl(out, in, 8);
    tcg_gen_sari_tl(out, out, 8);
    tcg_gen_deposit_tl(out, out, temp, 0, 16);

    tcg_temp_free(temp);
}

FOREACH_RR1(xsbh, gen_xsbh)
FOREACH_RR1(xshw, tcg_gen_ext16s_tl)

static ExitStatus insn_xswd(DisassContext *ctx, uint32_t insn)
{
    DISASS_RR1;

    tcg_gen_sari_tl(cpu_gpr[rt][0], cpu_gpr[ra][1], 31);
    tcg_gen_mov_tl(cpu_gpr[rt][1], cpu_gpr[ra][1]);
    tcg_gen_sari_tl(cpu_gpr[rt][2], cpu_gpr[ra][3], 31);
    tcg_gen_mov_tl(cpu_gpr[rt][3], cpu_gpr[ra][3]);
    return NO_EXIT;
}

FOREACH_RR(and, tcg_gen_and_tl)
FOREACH_RR(andc, tcg_gen_andc_tl)
FOREACH_RI10_ADJ(andbi, tcg_gen_and_tl, imm &= 0xff; imm *= 0x01010101)
FOREACH_RI10_ADJ(andhi, tcg_gen_and_tl, imm &= 0xffff; imm |= imm << 16)
FOREACH_RI10(andi, tcg_gen_and_tl)

FOREACH_RR(or, tcg_gen_or_tl)
FOREACH_RR(orc, tcg_gen_orc_tl)
FOREACH_RI10_ADJ(orbi, tcg_gen_or_tl, imm &= 0xff; imm *= 0x01010101)
FOREACH_RI10_ADJ(orhi, tcg_gen_or_tl, imm &= 0xffff; imm |= imm << 16)
FOREACH_RI10(ori, tcg_gen_or_tl)

static ExitStatus insn_orx(DisassContext *ctx, uint32_t insn)
{
    DISASS_RR1;

    tcg_gen_or_tl(cpu_gpr[rt][0], cpu_gpr[ra][0], cpu_gpr[ra][2]);
    tcg_gen_or_tl(cpu_gpr[rt][1], cpu_gpr[rt][1], cpu_gpr[ra][3]);
    tcg_gen_or_tl(cpu_gpr[rt][0], cpu_gpr[rt][0], cpu_gpr[ra][1]);
    tcg_gen_movi_tl(cpu_gpr[rt][1], 0);
    tcg_gen_movi_tl(cpu_gpr[rt][2], 0);
    tcg_gen_movi_tl(cpu_gpr[rt][3], 0);
    return NO_EXIT;
}

FOREACH_RR(xor, tcg_gen_xor_tl)
FOREACH_RI10_ADJ(xorbi, tcg_gen_xor_tl, imm &= 0xff; imm *= 0x01010101)
FOREACH_RI10_ADJ(xorhi, tcg_gen_xor_tl, imm &= 0xffff; imm |= imm << 16)
FOREACH_RI10(xori, tcg_gen_xor_tl)

FOREACH_RR(nand, tcg_gen_nand_tl)
FOREACH_RR(nor, tcg_gen_nor_tl)
FOREACH_RR(eqv, tcg_gen_eqv_tl)

/* ---------------------------------------------------------------------- */
/* Section 6: Shift and Rotate Instructions.  */

/* ---------------------------------------------------------------------- */
/* Section 7: Compare, Branch, and Halt Instructions.  */

/* ---------------------------------------------------------------------- */
/* Section 8: Hint for Branch Instructions.  */

/* ---------------------------------------------------------------------- */
/* Section 9: Floating-Point Instructions.  */

/* ---------------------------------------------------------------------- */
/* Section 10: Control Instructions.  */

/* ---------------------------------------------------------------------- */
/* Section 11: Channel Instructions.  */

/* ---------------------------------------------------------------------- */

typedef ExitStatus insn_fn(DisassContext *ctx, uint32_t insn);

/* Up to 11 bits are considered "opcode", depending on the format.
   To make things easy to pull out of the ISA document, we use a
   left-aligned 12-bits in the table below.  That makes it trivial
   to read the opcode directly from the left aligned ISA document.  */

typedef enum {
  FMT_RRR,
  FMT_RI18,
  FMT_RI10,
  FMT_RI16,
  FMT_RI8,
  FMT_RR,
  FMT_RI7,
  FMT_MAX
} InsnFormat;

typedef struct {
  InsnFormat fmt;
  insn_fn *fn;
} InsnDescr;

#undef INSN
#define INSN(opc, fmt, name) \
    [opc >> 1] = { FMT_##fmt, insn_##name }

static InsnDescr const translate_table[0x800] = {
    /* RRR Instruction Format (4-bit op).  */
    // INSN(0x800, RRR, selb),
    // INSN(0xb00, RRR, shufb),
    INSN(0xc00, RRR, mpya),
    // INSN(0xd00, RRR, fnms),
    // INSN(0xe00, RRR, fma),
    // INSN(0xf00, RRR, fms),

    /* RI18 Instruction Format (7-bit op).  */
    INSN(0x420, RI18, ila),
    // INSN(0x100, RI18, hbra),
    // INSN(0x120, RI18, hbrr),

    /* RI10 Instruction Format (8-bit op).  */
    INSN(0x340, RI10, lqd),
    INSN(0x240, RI10, stqd),

    INSN(0x1d0, RI10, ahi),
    INSN(0x1c0, RI10, ai),
    INSN(0x0d0, RI10, sfhi),
    INSN(0x0c0, RI10, sfi),

    INSN(0x740, RI10, mpyi),
    INSN(0x750, RI10, mpyui),

    INSN(0x160, RI10, andbi),
    INSN(0x150, RI10, andhi),
    INSN(0x140, RI10, andi),
    INSN(0x060, RI10, orbi),
    INSN(0x050, RI10, orhi),
    INSN(0x040, RI10, ori),
    INSN(0x460, RI10, xorbi),
    INSN(0x450, RI10, xorhi),
    INSN(0x440, RI10, xori),

    // INSN(0x7f0, RI10, heqi),
    // INSN(0x4f0, RI10, hgti),
    // INSN(0x5f0, RI10, hlgti),
    // INSN(0x7e0, RI10, ceqbi),
    // INSN(0x7d0, RI10, ceqhi),
    // INSN(0x7c0, RI10, ceqi),
    // INSN(0x4e0, RI10, cgtbi),
    // INSN(0x4d0, RI10, cgthi),
    // INSN(0x4c0, RI10, cgti),
    // INSN(0x5e0, RI10, clgtbi),
    // INSN(0x5d0, RI10, clgthi),
    // INSN(0x5c0, RI10, clgti),

    /* RI16 Instruction Format (9-bit op).  */
    INSN(0x308, RI16, lqa),
    INSN(0x338, RI16, lqr),
    INSN(0x208, RI16, stqa),
    INSN(0x238, RI16, stqr),

    INSN(0x418, RI16, ilh),
    INSN(0x410, RI16, ilhu),
    INSN(0x408, RI16, il),
    INSN(0x608, RI16, iohl),
    INSN(0x328, RI16, fsmbi),

    // INSN(0x320, RI16, br),
    // INSN(0x300, RI16, bra),
    // INSN(0x330, RI16, brsl),
    // INSN(0x310, RI16, brasl),
    // INSN(0x210, RI16, brnz),
    // INSN(0x200, RI16, brz),
    // INSN(0x230, RI16, brhnz),
    // INSN(0x220, RI16, brhz),

    /* RR/RI7 Instruction Format (11-bit op).  */
    INSN(0x388, RR, lqx),
    INSN(0x288, RR, stqx),

    // INSN(0x3e8, RR, cbd),
    // INSN(0x3a8, RR, cbx),
    // INSN(0x3ea, RR, chd),
    // INSN(0x3aa, RR, chx),
    // INSN(0x3ec, RR, cwd),
    // INSN(0x3ac, RR, cwx),
    // INSN(0x3ee, RR, cdd),
    // INSN(0x3ae, RR, cdx),

    INSN(0x190, RR, ah),
    INSN(0x180, RR, a),
    INSN(0x090, RR, sfh),
    INSN(0x080, RR, sf),
    INSN(0x680, RR, addx),
    INSN(0x184, RR, cg),
    INSN(0x684, RR, cgx),
    INSN(0x682, RR, sfx),
    INSN(0x084, RR, bg),
    INSN(0x686, RR, bgx),

    INSN(0x788, RR, mpy),
    INSN(0x798, RR, mpyu),
    INSN(0x78a, RR, mpyh),
    INSN(0x78e, RR, mpys),
    INSN(0x78c, RR, mpyhh),
    INSN(0x68c, RR, mpyhha),
    INSN(0x79c, RR, mpyhhu),
    INSN(0x69c, RR, mpyhhau),

    INSN(0x54a, RR, clz),
    INSN(0x568, RR, cntb),
    INSN(0x36c, RR, fsmb),
    INSN(0x36a, RR, fsmh),
    INSN(0x368, RR, fsm),
    INSN(0x364, RR, gbb),
    INSN(0x362, RR, gbh),
    INSN(0x360, RR, gb),
    INSN(0x1a6, RR, avgb),
    INSN(0x0a6, RR, absdb),
    INSN(0x4a6, RR, sumb),
    INSN(0x56c, RR, xsbh),
    INSN(0x55c, RR, xshw),
    INSN(0x54c, RR, xswd),
    INSN(0x182, RR, and),
    INSN(0x582, RR, andc),
    INSN(0x082, RR, or),
    INSN(0x592, RR, orc),
    INSN(0x3e0, RR, orx),
    INSN(0x482, RR, xor),
    INSN(0x192, RR, nand),
    INSN(0x092, RR, nor),
    INSN(0x492, RR, eqv),

    // INSN(0x0be, RR, shlh),
    // INSN(0x0fe, RR, shlhi),
    // INSN(0x0b6, RR, shl),
    // INSN(0x0f6, RR, shli),
    // INSN(0x3b6, RR, shlqbi),
    // INSN(0x3f6, RR, shlqbii),
    // INSN(0x3be, RR, shlqby),
    // INSN(0x3fe, RR, shlqbyi),
    // INSN(0x39e, RR, shlqbybi),
    // INSN(0x0b8, RR, roth),
    // INSN(0x0f8, RR, rothi),
    // INSN(0x0b0, RR, rot),
    // INSN(0x0f0, RR, roti),
    // INSN(0x3b8, RR, rotqby),
    // INSN(0x3f8, RR, rotqbyi),
    // INSN(0x398, RR, rotqbybi),
    // INSN(0x3b0, RR, rotqbi),
    // INSN(0x3f0, RR, rotqbii),
    // INSN(0x0ba, RR, rothm),
    // INSN(0x0fa, RR, rothmi),
    // INSN(0x0b2, RR, rotm),
    // INSN(0x0f2, RR, rotmi),
    // INSN(0x3ba, RR, rotqmby),
    // INSN(0x3fa, RR, rotqmbyi),
    // INSN(0x39a, RR, rotqmbybi),
    // INSN(0x3b2, RR, rotqmbi),
    // INSN(0x3f2, RR, rotqmbii),
    // INSN(0x0bc, RR, rotmah),
    // INSN(0x0fc, RR, rotmahi),
    // INSN(0x0b4, RR, rotma),
    // INSN(0x0f4, RR, rotmai),

    // INSN(0x7b0, RR, heq),
    // INSN(0x2b0, RR, hgt),
    // INSN(0x5b0, RR, hlgt),
    // INSN(0x7a0, RR, ceqb),
    // INSN(0x790, RR, ceqh),
    // INSN(0x780, RR, ceq),
    // INSN(0x4a0, RR, cgtb),
    // INSN(0x490, RR, cgth),
    // INSN(0x480, RR, cgt),
    // INSN(0x5a0, RR, clgtb),
    // INSN(0x590, RR, clgth),
    // INSN(0x580, RR, clgt),

    // INSN(0x350, RR, bi),
    // INSN(0x354, RR, iret),
    // INSN(0x356, RR, bisled),
    // INSN(0x352, RR, bisl),
    // INSN(0x250, RR, biz),
    // INSN(0x252, RR, binz),
    // INSN(0x254, RR, bihz),
    // INSN(0x256, RR, bihnz),

    // INSN(0x358, RR, hbr),

    // INSN(0x588, RR, FA),
    // INSN(0x598, RR, DFA),
    // INSN(0x58a, RR, FS),
    // INSN(0x59a, RR, DFS),
    // INSN(0x58c, RR, FM),
    // INSN(0x59c, RR, DFM),
    // INSN(0x6b8, RR, DFMA),
    // INSN(0x6bc, RR, DFNMS),
    // INSN(0x6ba, RR, DFMS),
    // INSN(0x6be, RR, DFNMA),
    // INSN(0x370, RR, FREST),
    // INSN(0x372, RR, FRSQEST),
    // INSN(0x7a8, RR, FI),
    // INSN(0x768, RR, CSFLT),
    // INSN(0x760, RR, CFLTS),
    // INSN(0x76c, RR, CUFLT),
    // INSN(0x764, RR, CFLTU),
    // INSN(0x772, RR, FRDS),
    // INSN(0x770, RR, FESD),
    // INSN(0x786, RR, DFCEQ),
    // INSN(0x796, RR, DFCMEQ),
    // INSN(0x586, RR, DFCGT),
    // INSN(0x596, RR, DFCMGT),
    // INSN(0x77e, RR, DFTSV),
    // INSN(0x784, RR, FCEQ),
    // INSN(0x794, RR, FCMEQ),
    // INSN(0x584, RR, FCGT),
    // INSN(0x594, RR, FCMGT),
    // INSN(0x774, RR, FSCRWR),
    // INSN(0x730, RR, FSCRRD),

    // INSN(0x000, RR, stop),
    // INSN(0x280, RR, stopd),
    // INSN(0x002, RR, lnop),
    // INSN(0x402, RR, nop),
    // INSN(0x004, RR, sync),
    // INSN(0x006, RR, dsync),
    // INSN(0x018, RR, mfspr),
    // INSN(0x218, RR, mtspr),

    // INSN(0x01a, RR, rdch),
    // INSN(0x01e, RR, rchcnt),
    // INSN(0x21a, RR, wrch),
};

static const InsnDescr *translate_0(uint32_t insn)
{
    static uint32_t const fmt_mask[FMT_MAX] = {
        [FMT_RRR]  = 0x780,
        [FMT_RI18] = 0x7f0,
        [FMT_RI10] = 0x7f8,
        [FMT_RI16] = 0x7fc,
        [FMT_RI8]  = 0x7fe,
        [FMT_RR]   = 0x7ff,
        [FMT_RI7]  = 0x7ff
    };

    uint32_t op = insn >> 21;
    InsnFormat fmt;
    const InsnDescr *desc;

    /* Sadly, except for certain cases, one cannot look at special
       bits within the opcode that indicate the format.  So we try
       matching with increasing opcode width until we get a match.  */
    for (fmt = 0; fmt < FMT_MAX; ++fmt) {
        desc = &translate_table[op & fmt_mask[fmt]];
        if (desc->fn && desc->fmt == fmt) {
            return desc;
        }
    }

    return NULL;
}

static ExitStatus translate_1(DisassContext *ctx, uint32_t insn)
{
    const InsnDescr *desc = translate_0(insn);

    if (desc == NULL) {
        qemu_log("Unimplemented opcode %#3x\n", insn >> 20);
        return NO_EXIT;
    }

    return desc->fn(ctx, insn);
}

void gen_intermediate_code(CPUSPUState *env, struct TranslationBlock *tb)
{
    SPUCPU *cpu = spu_env_get_cpu(env);
    CPUState *cs = CPU(cpu);
    DisassContext ctx;
    uint32_t pc_start;
    uint32_t insn;
    ExitStatus ret;
    int num_insns;
    int max_insns;

    ctx.tb = tb;
    ctx.pc = pc_start = tb->pc;
    ctx.lslr = env->lslr;
    ctx.singlestep = cs->singlestep_enabled;

    if (ctx.singlestep || singlestep) {
        max_insns = 1;
    } else {
        num_insns = (TARGET_PAGE_SIZE - (pc_start & ~TARGET_PAGE_MASK)) / 4;
        max_insns = tb->cflags & CF_COUNT_MASK ? : CF_COUNT_MASK;
        if (max_insns == 0) {
            max_insns = CF_COUNT_MASK;
        }
        if (max_insns > num_insns) {
            max_insns = num_insns;
        }
        if (max_insns > TCG_MAX_INSNS) {
            max_insns = TCG_MAX_INSNS;
        }
    }
    num_insns = 0;

    qemu_log_mask(CPU_LOG_TB_IN_ASM, "IN: %s\n", lookup_symbol(pc_start));

    gen_tb_start(tb);
    do {
        tcg_gen_insn_start(ctx.pc);
        num_insns++;
        
        if (unlikely(cpu_breakpoint_test(cs, ctx.pc, BP_ANY))) {
            tcg_gen_movi_tl(cpu_pc, ctx.pc);
            gen_helper_debug(cpu_env);
            ret = EXIT_NORETURN;
            ctx.pc += 4;
            break;
        }
        if (num_insns + 1 == max_insns && (tb->cflags & CF_LAST_IO)) {
            gen_io_start();
        }
        insn = cpu_ldl_code(env, ctx.pc);

        ret = translate_1(&ctx, insn);
        ctx.pc += 4;

        /* If we exhaust instruction count, stop generation.  */
        if (ret == NO_EXIT && (num_insns >= max_insns || tcg_op_buf_full())) {
            ret = EXIT_PC_STALE;
        }
    } while (ret == NO_EXIT);

    if (tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }

    switch (ret) {
    case EXIT_GOTO_TB:
    case EXIT_NORETURN:
        break;
    case EXIT_PC_STALE:
        tcg_gen_movi_tl(cpu_pc, ctx.pc);
        /* FALLTHRU */
    case EXIT_PC_UPDATED:
        if (ctx.singlestep) {
            gen_helper_debug(cpu_env);
        } else {
            tcg_gen_exit_tb(0);
        }
        break;
    default:
        abort();
    }

    gen_tb_end(tb, num_insns);
    tb->size = ctx.pc - pc_start;
    tb->icount = num_insns;

    qemu_log_mask(CPU_LOG_TB_IN_ASM, "\n");
}

void spu_translate_init(void)
{
    static bool done_init;
    int i, j;

    if (done_init) {
        return;
    }
    done_init = true;

    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");
    cpu_pc = tcg_global_mem_new(cpu_env, offsetof(CPUSPUState, pc), "pc");
    cpu_srr0 = tcg_global_mem_new(cpu_env, offsetof(CPUSPUState, srr0),
                                  "srr0");

    for (i = 0; i < 128; i++) {
        for (j = 0; j < 4; ++j) {
            sprintf(cpu_reg_names[i][j], "$%d:%d", i, j);
            cpu_gpr[i][j]
                = tcg_global_mem_new(cpu_env,
                                     offsetof(CPUSPUState, gpr[i*4+j]),
                                     cpu_reg_names[i][j]);
        }
    }
}

void restore_state_to_opc(CPUSPUState *env, TranslationBlock *tb,
                          target_ulong *data)
{
    env->pc = data[0];
}

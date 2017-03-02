/*
 * Tiny Code Interpreter for QEMU
 *
 * Copyright (c) 2009, 2011, 2016 Stefan Weil
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

/* Enable TCI assertions only when debugging TCG (and without NDEBUG defined).
 * Without assertions, the interpreter runs much faster. */
#if defined(CONFIG_DEBUG_TCG)
# define tci_assert(cond) assert(cond)
#else
# define tci_assert(cond) ((void)0)
#endif

#include "qemu-common.h"
#include "tcg/tcg.h"           /* MAX_OPC_PARAM_IARGS */
#include "exec/cpu_ldst.h"
#include "qemu/tci.h"
#include <ffi.h>

uintptr_t tci_tb_ptr = 0;

#define ldul_le_p(p)  ((uint32_t)ldl_le_p(p))
#define ldul_be_p(p)  ((uint32_t)ldl_be_p(p))

#ifdef CONFIG_SOFTMMU
# define qemu_ld(A, MMU, BIT, FAST, SLOW)                                    \
    ({ target_ulong adr = A; uint##BIT##_t ret;                              \
       target_ulong adr_mask = TARGET_PAGE_MASK | ((BIT / 8) - 1);           \
       uintptr_t tlb_idx = (adr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);   \
       const CPUTLBEntry *tlb = &env->tlb_table[MMU][tlb_idx];               \
       if (likely((adr & adr_mask) == tlb->addr_read)) {                     \
           ret = FAST((void *)((uintptr_t)adr + tlb->addend));               \
       } else {                                                              \
           ret = SLOW(env, adr, MMU, (uintptr_t)pc);                         \
       }                                                                     \
       ret; })

# define qemu_ldub(A,M)    qemu_ld(A,M, 8, ldub_p, helper_ret_ldub_mmu)
# define qemu_lduw_le(A,M) qemu_ld(A,M, 16, lduw_le_p, helper_le_lduw_mmu)
# define qemu_lduw_be(A,M) qemu_ld(A,M, 16, lduw_be_p, helper_be_lduw_mmu)
# define qemu_ldul_le(A,M) qemu_ld(A,M, 32, ldul_le_p, helper_le_ldul_mmu)
# define qemu_ldul_be(A,M) qemu_ld(A,M, 32, ldul_be_p, helper_be_ldul_mmu)
# define qemu_ldq_le(A,M)  qemu_ld(A,M, 64, ldq_le_p, helper_le_ldq_mmu)
# define qemu_ldq_be(A,M)  qemu_ld(A,M, 64, ldq_be_p, helper_be_ldq_mmu)

# define qemu_st(V, A, MMU, BIT, FAST, SLOW)                                 \
    ({ target_ulong adr = A;                                                 \
       target_ulong adr_mask = TARGET_PAGE_MASK | ((BIT / 8) - 1);           \
       uintptr_t tlb_idx = (adr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);   \
       const CPUTLBEntry *tlb = &env->tlb_table[MMU][tlb_idx];               \
       if (likely((adr & adr_mask) == tlb->addr_write)) {                    \
           FAST((void *)((uintptr_t)adr + tlb->addend), V);                  \
       } else {                                                              \
           SLOW(env, adr, V, MMU, (uintptr_t)pc);                            \
       }                                                                     \
       (void)0; })

# define qemu_stb(V,A,M)    qemu_st(V,A,M,  8, stb_p, helper_ret_stb_mmu)
# define qemu_stw_le(V,A,M) qemu_st(V,A,M, 16, stw_le_p, helper_le_stw_mmu)
# define qemu_stw_be(V,A,M) qemu_st(V,A,M, 16, stq_be_p, helper_be_stw_mmu)
# define qemu_stl_le(V,A,M) qemu_st(V,A,M, 32, stl_le_p, helper_le_stl_mmu)
# define qemu_stl_be(V,A,M) qemu_st(V,A,M, 32, stl_be_p, helper_be_stl_mmu)
# define qemu_stq_le(V,A,M) qemu_st(V,A,M, 64, stq_le_p, helper_le_stq_mmu)
# define qemu_stq_be(V,A,M) qemu_st(V,A,M, 64, stq_be_p, helper_be_stq_mmu)
#else

# define qemu_ldub(A,M)     ldub_p(g2h((target_ulong)A))
# define qemu_lduw_le(A,M)  lduw_le_p(g2h((target_ulong)A))
# define qemu_lduw_be(A,M)  lduw_be_p(g2h((target_ulong)A))
# define qemu_ldul_le(A,M)  ldul_le_p(g2h((target_ulong)A))
# define qemu_ldul_be(A,M)  ldul_be_p(g2h((target_ulong)A))
# define qemu_ldq_le(A,M)   ldq_le_p(g2h((target_ulong)A))
# define qemu_ldq_be(A,M)   ldq_be_p(g2h((target_ulong)A))
# define qemu_stb(V,A,M)    stb_p(g2h((target_ulong)A), V)
# define qemu_stw_le(V,A,M) stw_le_p(g2h((target_ulong)A), V)
# define qemu_stw_be(V,A,M) stw_be_p(g2h((target_ulong)A), V)
# define qemu_stl_le(V,A,M) stl_le_p(g2h((target_ulong)A), V)
# define qemu_stl_be(V,A,M) stl_be_p(g2h((target_ulong)A), V)
# define qemu_stq_le(V,A,M) stq_le_p(g2h((target_ulong)A), V)
# define qemu_stq_be(V,A,M) stq_be_p(g2h((target_ulong)A), V)
#endif

static inline uint64_t concat4(uint32_t x, uint32_t y)
{
    return deposit64(y, 32, 32, x);
}

static inline uintptr_t concatp(uint32_t x, uint32_t y)
{
    if (sizeof(void *) == 8) {
        return concat4(x, y);
    }
    return y;
}

#define MAX_CALL_ARGS  (TCG_STATIC_CALL_ARGS_SIZE / sizeof(uint64_t))

/* Interpret pseudo code in tb. */
uintptr_t tcg_qemu_tb_exec(CPUArchState *env, uint8_t *pc8)
{
    uint64_t sp[MAX_CALL_ARGS + CPU_TEMP_BUF_NLONGS];
    void *sp_slots[MAX_CALL_ARGS];
    tcg_target_ulong regs[8];

    int32_t *pc = (int32_t *)pc8;
    tcg_target_ulong r, w, x, y;
    intptr_t ri, wi, xi, yi;
    bool cmp = false;
    void *ptr;
    uint64_t t64;
    uint32_t insn;
    TCIOp opc;

    sp_slots[0] = NULL;
    regs[6] = (uintptr_t)env;
    regs[7] = (uintptr_t)sp;
    goto next;

 output_rw:
    assert(wi < 6);
    regs[wi] = w;
 output_r:
    assert(ri < 6);
    regs[ri] = r;
 next:
    insn = *pc++;

    opc = extract32(insn, POS_OP, LEN_OP);
    ri  = extract32(insn, POS_R, LEN_R);
    wi  = extract32(insn, POS_W, LEN_W);
    xi  = extract32(insn, POS_X, LEN_X);
    yi  = extract32(insn, POS_Y, LEN_Y);

    w = regs[wi];

    if (likely(xi < 8)) {
        x = regs[xi];
    } else if (xi == 8) {
        x = *pc++;
    } else {
        x = xi - BIAS_X;
    }

    if (likely(yi < 8)) {
        y = regs[yi];
    } else if (yi == 8) {
        y = *pc++;
    } else {
        y = yi - BIAS_Y;
    }

    ptr = (void *)((uintptr_t)w + (uintptr_t)y);

    switch (opc) {
    /*
     * Normal binary operations
     */
    case TCI_add:
        r = x + y;
        goto output_r;
    case TCI_sub:
        r = x - y;
        goto output_r;
    case TCI_mul:
        r = x * y;
        goto output_r;
    case TCI_divu:
        r = x / y;
        goto output_r;
    case TCI_remu:
        r = x % y;
        goto output_r;
    case TCI_divs:
        r = (tcg_target_long)x / (tcg_target_long)y;
        goto output_r;
    case TCI_rems:
        r = (tcg_target_long)x % (tcg_target_long)y;
        goto output_r;
    case TCI_and:
        r = x & y;
        goto output_r;
    case TCI_ior:
        r = x | y;
        goto output_r;
    case TCI_xor:
        r = x ^ y;
        goto output_r;
    case TCI_andc:
        r = x & ~y;
        goto output_r;
    case TCI_iorc:
        r = x | ~y;
        goto output_r;
    case TCI_xorc:
        r = x ^ ~y;
        goto output_r;
    case TCI_nand:
        r = ~(x & y);
        goto output_r;
    case TCI_nior:
        r = ~(x | y);
        goto output_r;
    case TCI_shl:
        r = x << (y & (TCG_TARGET_REG_BITS - 1));
        goto output_r;
    case TCI_shr4:
        r = (uint32_t)x >> (y & 31);
        goto output_r;
    case TCI_sar4:
        r = (int32_t)x >> (y & 31);
        goto output_r;
    case TCI_rol4:
        r = rol32(x, y & 31);
        goto output_r;
    case TCI_ror4:
        r = ror32(x, y & 31);
        goto output_r;
    case TCI_movc:
        r = (cmp ? x : y);
        goto output_r;
#if TCG_TARGET_REG_BITS == 64
    case TCI_shr8:
        r = x >> (y & 63);
        goto output_r;
    case TCI_sar8:
        r = (int64_t)x >> (y & 63);
        goto output_r;
    case TCI_rol8:
        r = rol64(x, y & 63);
        goto output_r;
    case TCI_ror8:
        r = ror64(x, y & 63);
        goto output_r;
    case TCI_concat4:
        r = concat4(x, y);
        goto output_r;
#endif /* 64 */

    case TCI_extract:
        {
            int pos = y >> 6;
            int len = y & 0x3f;
            if (TCG_TARGET_REG_BITS == 32) {
                r = extract32(x, pos, len);
            } else {
                r = extract64(x, pos, len);
            }
        }
        goto output_r;
    case TCI_sextract:
        {
            int pos = y >> 6;
            int len = y & 0x3f;
            if (TCG_TARGET_REG_BITS == 32) {
                r = sextract32(x, pos, len);
            } else {
                r = sextract64(x, pos, len);
            }
        }
        goto output_r;

    case TCI_ctz:
        r = (x ? (TCG_TARGET_REG_BITS == 64 ? ctz64(x) : ctz32(x)) : y);
        goto output_r;
    case TCI_clz:
        r = (x ? (TCG_TARGET_REG_BITS == 64 ? clz64(x) : clz32(x)) : y);
        goto output_r;

    /*
     * Comparison operations
     */
    case TCI_cmp4eq:
        cmp = ((int32_t)x == (int32_t)y);
        goto next;
    case TCI_cmp4ne:
        cmp = ((int32_t)x != (int32_t)y);
        goto next;
    case TCI_cmp4lt:
        cmp = ((int32_t)x < (int32_t)y);
        goto next;
    case TCI_cmp4le:
        cmp = ((int32_t)x <= (int32_t)y);
        goto next;
    case TCI_cmp4gt:
        cmp = ((int32_t)x > (int32_t)y);
        goto next;
    case TCI_cmp4ge:
        cmp = ((int32_t)x >= (int32_t)y);
        goto next;
    case TCI_cmp4ltu:
        cmp = ((uint32_t)x < (uint32_t)y);
        goto next;
    case TCI_cmp4leu:
        cmp = ((uint32_t)x <= (uint32_t)y);
        goto next;
    case TCI_cmp4gtu:
        cmp = ((uint32_t)x > (uint32_t)y);
        goto next;
    case TCI_cmp4geu:
        cmp = ((uint32_t)x >= (uint32_t)y);
        goto next;
#if TCG_TARGET_REG_BITS == 64
    case TCI_cmp8eq:
        cmp = ((int64_t)x == (int64_t)y);
        goto next;
    case TCI_cmp8ne:
        cmp = ((int64_t)x != (int64_t)y);
        goto next;
    case TCI_cmp8lt:
        cmp = ((int64_t)x < (int64_t)y);
        goto next;
    case TCI_cmp8le:
        cmp = ((int64_t)x <= (int64_t)y);
        goto next;
    case TCI_cmp8gt:
        cmp = ((int64_t)x > (int64_t)y);
        goto next;
    case TCI_cmp8ge:
        cmp = ((int64_t)x >= (int64_t)y);
        goto next;
    case TCI_cmp8ltu:
        cmp = ((uint64_t)x < (uint64_t)y);
        goto next;
    case TCI_cmp8leu:
        cmp = ((uint64_t)x <= (uint64_t)y);
        goto next;
    case TCI_cmp8gtu:
        cmp = ((uint64_t)x > (uint64_t)y);
        goto next;
    case TCI_cmp8geu:
        cmp = ((uint64_t)x >= (uint64_t)y);
        goto next;
#endif /* 64 */

    /*
     * Unary operations
     */

    case TCI_bswap2:
        r = bswap16(y);
        goto output_r;
    case TCI_bswap4:
        r = bswap32(y);
        goto output_r;
    case TCI_bswap8:
        r = bswap64(y);
        goto output_r;

    case TCI_ctpop:
        r = (TCG_TARGET_REG_BITS == 64 ? ctpop64(y) : ctpop32(y));
        goto output_r;

    /*
     * Zero-ary operation
     */

    case TCI_setc:
        r = cmp;
        goto output_r;

    case TCI_mb:
        smp_mb();
        goto next;

    /*
     * Trinary operation
     */

    case TCI_deposit:
        {
            int pos = y >> 6;
            int len = y & 0x3f;
            if (TCG_TARGET_REG_BITS == 32) {
                r = deposit32(w, pos, len, x);
            } else {
                r = deposit64(w, pos, len, x);
            }
        }
        goto output_r;

    /*
     * QEMU store operations
     */

    case TCI_qst1:
        qemu_stb(x, w, y);
        goto next;
    case TCI_qst2_le:
        qemu_stw_le(x, w, y);
        goto next;
    case TCI_qst2_be:
        qemu_stw_be(x, w, y);
        goto next;
    case TCI_qst4_le:
        qemu_stl_le(x, w, y);
        goto next;
    case TCI_qst4_be:
        qemu_stl_be(x, w, y);
        goto next;
    case TCI_qst8_le:
        if (TCG_TARGET_REG_BITS == 64) {
            qemu_stq_le(x, w, y);
        } else {
            t64 = concat4(regs[ri], x);
            qemu_stq_le(t64, w, y);
        }
        goto next;
    case TCI_qst8_be:
        if (TCG_TARGET_REG_BITS == 64) {
            qemu_stq_be(x, w, y);
        } else {
            t64 = concat4(regs[ri], x);
            qemu_stq_be(t64, w, y);
        }
        goto next;

    /*
     * QEMU load operations
     */

    case TCI_qld1u:
        r = qemu_ldub(x, y);
        goto output_r;
    case TCI_qld1s:
        r = (int8_t)qemu_ldub(x, y);
        goto output_r;
    case TCI_qld2u_le:
        r = qemu_lduw_le(x, y);
        goto output_r;
    case TCI_qld2u_be:
        r = qemu_lduw_be(x, y);
        goto output_r;
    case TCI_qld2s_le:
        r = (int16_t)qemu_lduw_le(x, y);
        goto output_r;
    case TCI_qld2s_be:
        r = (int16_t)qemu_lduw_be(x, y);
        goto output_r;
    case TCI_qld4u_le:
        r = qemu_ldul_le(x, y);
        goto output_r;
    case TCI_qld4u_be:
        r = qemu_ldul_be(x, y);
        goto output_r;
    case TCI_qld4s_le:
        r = (int32_t)qemu_ldul_le(x, y);
        goto output_r;
    case TCI_qld4s_be:
        r = (int32_t)qemu_ldul_be(x, y);
        goto output_r;
    case TCI_qld8_le:
        r = t64 = qemu_ldq_le(x, y);
        if (TCG_TARGET_REG_BITS == 32) {
            w = t64 >> 32;
            goto output_rw;
        }
        goto output_r;
    case TCI_qld8_be:
        r = t64 = qemu_ldq_be(x, y);
        if (TCG_TARGET_REG_BITS == 32) {
            w = t64 >> 32;
            goto output_rw;
        }
        goto output_r;

    /*
     * Normal stores - note that these must be naturally aligned
     */

    case TCI_st1:
        *(uint8_t *)ptr = x;
        goto next;
    case TCI_st2:
        *(uint16_t *)ptr = x;
        goto next;
    case TCI_st4:
        *(uint32_t *)ptr = x;
        goto next;
#if TCG_TARGET_REG_BITS == 64
    case TCI_st8:
        *(uint64_t *)ptr = x;
        goto next;
#endif /* 64 */

    /*
     * Normal loads - note that these must be naturally aligned
     */

    case TCI_ld1u:
        r = *(uint8_t *)ptr;
        goto output_r;
    case TCI_ld1s:
        r = *(int8_t *)ptr;
        goto output_r;
    case TCI_ld2u:
        r = *(uint16_t *)ptr;
        goto output_r;
    case TCI_ld2s:
        r = *(int16_t *)ptr;
        goto output_r;
    case TCI_ld4u:
        r = *(uint32_t *)ptr;
        goto output_r;
    case TCI_ld4s:
        r = *(int32_t *)ptr;
        goto output_r;
#if TCG_TARGET_REG_BITS == 64
    case TCI_ld8:
        r = *(uint64_t *)ptr;
        goto output_r;
#endif /* 64 */

    /*
     * Control flow operations
     */

    case TCI_bc:
        if (!cmp) {
            goto next;
        }
        /* fall through */
    case TCI_b:
        pc += (ptrdiff_t)y;
        goto next;

    case TCI_exit:
        return concatp(x, y);

    case TCI_call0:
    case TCI_call4:
    case TCI_call8:
    {
        /* We're passed a pointer to the TCGHelperInfo, which contains
           the function pointer followed by the ffi_cif pointer.  */
        /* ??? Put the TCGHelperInfo struct somewhere it can be shared
           between tcg.c and tci.c, but without pulling in <ffi.h> to
           every user of tcg.h.  */
        void **pptr = (void **)concatp(x, y);

        /* Helper functions may need access to the "return address". */
        tci_tb_ptr = (uintptr_t)pc;

        /* Set up the ffi_avalue array once.  In tcg_gen_callN, we arranged
           for every real argument to be "left-aligned" in each 64-bit slot.  */
        if (sp_slots[0] == NULL) {
            int i;
            for (i = 0; i < MAX_CALL_ARGS; ++i) {
                sp_slots[i] = &sp[i];
            }
        }

        /* Call the helper function.  Any result winds up "left-aligned"
           in the sp[0] slot.  */
        ffi_call(pptr[1], pptr[0], &sp[0], sp_slots);

        if (opc == TCI_call8) {
            r = t64 = sp[0];
            if (TCG_TARGET_REG_BITS == 32) {
                w = t64 >> 32;
                goto output_rw;
            }
        } else {
            r = *(uint32_t *)sp;
        }
        goto output_r;

    case TCI_goto_tb:
        pc = *(void **)concatp(x, y);
        goto next;
    case TCI_goto_ptr:
        pc = (void *)(uintptr_t)y;
        if (unlikely(pc == NULL)) {
            return 0;
        }
        goto next;
    }

    /*
     * Widening multiply operations
     */

    case TCI_mulu2:
        if (TCG_TARGET_REG_BITS == 32) {
            r = t64 = (uint64_t)(uint32_t)x * (uint32_t)y;
            w = t64 >> 32;
        } else {
            uint64_t l, h;
            mulu64(&l, &h, x, y);
            r = l, w = h;
        }
        goto output_rw;
    case TCI_muls2:
        if (TCG_TARGET_REG_BITS == 32) {
            r = t64 = (uint64_t)(int32_t)x * (int32_t)y;
            w = t64 >> 32;
        } else {
            uint64_t l, h;
            muls64(&l, &h, x, y);
            r = l, w = h;
        }
        goto output_rw;

    /*
     * 2-input double-word operations.  The two inputs are w:r and y:x.
     */
    default:
        r = regs[ri];
        switch (opc) {
#if TCG_TARGET_REG_BITS == 32
        case TCI_cmppeq:
            cmp = (r == y && w == x);
            goto next;
        case TCI_cmppne:
            cmp = (r != y || w != x);
            goto next;
        case TCI_cmpplt:
            cmp = ((int32_t)w < (int32_t)x || (w == x && r < y));
            goto next;
        case TCI_cmpple:
            cmp = ((int32_t)w < (int32_t)x || (w == x && r <= y));
            goto next;
        case TCI_cmppgt:
            cmp = ((int32_t)w > (int32_t)x || (w == x && r > y));
            goto next;
        case TCI_cmppge:
            cmp = ((int32_t)w > (int32_t)x || (w == x && r >= y));
            goto next;
        case TCI_cmppltu:
            cmp = (w < x || (w == x && r < y));
            goto next;
        case TCI_cmppleu:
            cmp = (w < x || (w == x && r <= y));
            goto next;
        case TCI_cmppgtu:
            cmp = (w > x || (w == x && r > y));
            goto next;
        case TCI_cmppgeu:
            cmp = (w > x || (w == x && r >= y));
            goto next;
#endif /* 32 */
        case TCI_add2:
            r += y;
            w += x + (r < y);
            goto output_rw;
        case TCI_sub2:
            w = w - x - (r < y);
            r -= y;
            goto output_rw;

        default:
            break;
        }
    }
    /* Should have looped back via goto.  */
    abort();
}

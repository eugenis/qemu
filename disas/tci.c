/*
 * Tiny Code Interpreter for QEMU - disassembler
 *
 * Copyright (c) 2011 Stefan Weil
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
#include "qemu-common.h"
#include "disas/bfd.h"
#include "tcg/tcg.h"
#include "qemu/tci.h"


#define O(OPC)  [TCI_##OPC] = #OPC

static const char * const opcode_name[1 << LEN_OP] = {
    O(invalid),
    O(add),
    O(sub),
    O(mul),
    O(divu),
    O(remu),
    O(divs),
    O(rems),
    O(and),
    O(ior),
    O(xor),
    O(andc),
    O(iorc),
    O(xorc),
    O(nand),
    O(nior),
    O(shl),
    O(shr4),
    O(sar4),
    O(rol4),
    O(ror4),
    O(shr8),
    O(sar8),
    O(rol8),
    O(ror8),
    O(cmp4eq),
    O(cmp4ne),
    O(cmp4lt),
    O(cmp4le),
    O(cmp4gt),
    O(cmp4ge),
    O(cmp4ltu),
    O(cmp4leu),
    O(cmp4gtu),
    O(cmp4geu),
    O(cmp8eq),
    O(cmp8ne),
    O(cmp8lt),
    O(cmp8le),
    O(cmp8gt),
    O(cmp8ge),
    O(cmp8ltu),
    O(cmp8leu),
    O(cmp8gtu),
    O(cmp8geu),
    O(qst1),
    O(qst2_le),
    O(qst2_be),
    O(qst4_le),
    O(qst4_be),
    O(qst8_le),
    O(qst8_be),
    O(extract),
    O(sextract),
    O(ctz),
    O(clz),
    O(movc),
    O(deposit),
    O(concat4),
    O(ctpop),
    O(bswap2),
    O(bswap4),
    O(bswap8),
    O(qld1u),
    O(qld1s),
    O(qld2u_le),
    O(qld2u_be),
    O(qld2s_le),
    O(qld2s_be),
    O(qld4u_le),
    O(qld4u_be),
    O(qld4s_le),
    O(qld4s_be),
    O(qld8_le),
    O(qld8_be),
    O(b),
    O(bc),
    O(exit),
    O(call0),
    O(call4),
    O(call8),
    O(goto_tb),
    O(goto_ptr),
    O(setc),
    O(mb),
    O(st1),
    O(st2),
    O(st4),
    O(st8),
    O(ld1u),
    O(ld1s),
    O(ld2u),
    O(ld2s),
    O(ld4u),
    O(ld4s),
    O(ld8),
    O(cmppeq),
    O(cmppne),
    O(cmpplt),
    O(cmpple),
    O(cmppgt),
    O(cmppge),
    O(cmppltu),
    O(cmppleu),
    O(cmppgtu),
    O(cmppgeu),
    O(add2),
    O(sub2),
    O(mulu2),
    O(muls2),
};

#undef O

static char const reg_names[8][4] = {
    "a", "b", "c", "d", "e", "f", "vp", "sp",
};

static inline const char *get_r(int n)
{
    assert(n < 8);
    return &reg_names[n][0];
}

static inline uint64_t concat4(uint32_t xv, uint32_t yv)
{
    return deposit64(yv, 32, 32, xv);
}

static inline void *concatp(int32_t xv, int32_t yv)
{
    if (sizeof(void *) == 8) {
        return (void *)(uintptr_t)concat4(xv, yv);
    }
    return (void *)(uintptr_t)yv;
}

/* Disassemble TCI bytecode. */
int print_insn_tci(bfd_vma baddr, disassemble_info *info)
{
    /* These are host address.  Dispense with the indirection.  */
    uint32_t *addr = (uint32_t *)(uintptr_t)baddr;
    uint32_t *orig_addr = addr;
    char buf1[16], buf2[16];
    const char *name, *r, *w, *x, *y;
    int ri, wi, xi, yi;
    int32_t xv, yv;
    uint32_t insn;
    TCIOp opc;

    insn = *addr++;

    opc = extract32(insn, POS_OP, LEN_OP);
    ri  = extract32(insn, POS_R, LEN_R);
    wi  = extract32(insn, POS_W, LEN_W);
    xi  = extract32(insn, POS_X, LEN_X);
    yi  = extract32(insn, POS_Y, LEN_Y);

    name = opcode_name[opc];
    r = reg_names[ri];
    w = reg_names[wi];

    if (xi < 8) {
        x = reg_names[xi];
        xv = 0xdeadbeef;
    } else {
        if (xi == 8) {
            xv = (int32_t)*addr++;
        } else {
            xv = xi - BIAS_X;
        }
        snprintf(buf1, sizeof(buf1), "%d", xv);
        x = buf1;
    }

    if (yi < 8) {
        y = reg_names[yi];
        yv = 0xdeadbeef;
    } else {
        if (yi == 8) {
            yv = (int32_t)*addr++;
        } else {
            yv = yi - BIAS_Y;
        }
        snprintf(buf2, sizeof(buf2), "%d", yv);
        y = buf2;
    }

    switch (opc) {
    case TCI_cmp4eq:
    case TCI_cmp4ne:
    case TCI_cmp4lt:
    case TCI_cmp4le:
    case TCI_cmp4gt:
    case TCI_cmp4ge:
    case TCI_cmp4ltu:
    case TCI_cmp4leu:
    case TCI_cmp4gtu:
    case TCI_cmp4geu:
    case TCI_cmp8eq:
    case TCI_cmp8ne:
    case TCI_cmp8lt:
    case TCI_cmp8le:
    case TCI_cmp8gt:
    case TCI_cmp8ge:
    case TCI_cmp8ltu:
    case TCI_cmp8leu:
    case TCI_cmp8gtu:
    case TCI_cmp8geu:
        info->fprintf_func(info->stream, "%-10s%s,%s", name, x, y);
        break;

    case TCI_qst8_le:
    case TCI_qst8_be:
        if (TCG_TARGET_REG_BITS == 32) {
            info->fprintf_func(info->stream, "%-10s[%s,%s]=%s:%s",
                               name, w, y, r, x);
            break;
        }
        /* FALLTHRU */
    case TCI_qst1:
    case TCI_qst2_le:
    case TCI_qst2_be:
    case TCI_qst4_le:
    case TCI_qst4_be:
        info->fprintf_func(info->stream, "%-10s[%s,%s]=%s", name, w, y, x);
        break;

    case TCI_qld8_le:
    case TCI_qld8_be:
        if (TCG_TARGET_REG_BITS == 32) {
            info->fprintf_func(info->stream, "%-10s%s:%s=[%s,%s]",
                               name, w, r, x, y);
            break;
        }
        /* FALLTHRU */
    case TCI_qld1u:
    case TCI_qld1s:
    case TCI_qld2u_le:
    case TCI_qld2u_be:
    case TCI_qld2s_le:
    case TCI_qld2s_be:
    case TCI_qld4u_le:
    case TCI_qld4u_be:
    case TCI_qld4s_le:
    case TCI_qld4s_be:
        info->fprintf_func(info->stream, "%-10s%s=[%s,%s]", name, r, x, y);
        break;

    case TCI_deposit:
        assert(yi >= 8);
        info->fprintf_func(info->stream, "%-10s%s=%s,%d,%d,%s",
                           name, r, w, yv >> 6, yv & 0x3f, x);
        break;
    case TCI_extract:
        assert(yi >= 8);
        info->fprintf_func(info->stream, "%-10s%s=%s,%d,%d",
                           name, r, x, yv >> 6, yv & 0x3f);
        break;
    case TCI_sextract:
        assert(yi >= 8);
        info->fprintf_func(info->stream, "%-10s%s=%s,%d,%d",
                           name, r, x, yv >> 6, yv & 0x3f);
        break;

    case TCI_concat4:
        if (xi >= 8 && yi >= 8) {
            /* Special case R = X:Y as MOV.  */
            info->fprintf_func(info->stream, "%-10s%s=0x%016" PRIx64,
                               "mov", r, concat4(xv, yv));
        } else {
            info->fprintf_func(info->stream, "%-10s%s=%s:%s", name, r, x, y);
        }
        break;

    case TCI_ior:
        if (xi == BIAS_X) {
            /* Special case R = 0 | Y as MOV.  */
            info->fprintf_func(info->stream, "%-10s%s=%s", "mov", r, y);
            break;
        }
        /* FALLTHRU */
    case TCI_add:
    case TCI_sub:
    case TCI_mul:
    case TCI_divu:
    case TCI_remu:
    case TCI_divs:
    case TCI_rems:
    case TCI_and:
    case TCI_xor:
    case TCI_andc:
    case TCI_iorc:
    case TCI_xorc:
    case TCI_nand:
    case TCI_nior:
    case TCI_shl:
    case TCI_shr4:
    case TCI_sar4:
    case TCI_rol4:
    case TCI_ror4:
    case TCI_shr8:
    case TCI_sar8:
    case TCI_rol8:
    case TCI_ror8:
    case TCI_movc:
    case TCI_clz:
    case TCI_ctz:
        info->fprintf_func(info->stream, "%-10s%s=%s,%s", name, r, x, y);
        break;

    case TCI_ctpop:
    case TCI_bswap2:
    case TCI_bswap4:
    case TCI_bswap8:
        info->fprintf_func(info->stream, "%-10s%s=%s", name, r, y);
        break;

    case TCI_b:
        /* Special case B with Y extention word as GOTO.  */
        if (yi == 8) {
            name = "goto";
        }
        /* FALLTHRU */
    case TCI_bc:
        assert(yi >= 8);
        info->fprintf_func(info->stream, "%-10s%p", name, addr + yv);
        break;

    case TCI_exit:
    case TCI_call0:
        assert(xi >= 8 && yi >= 8);
        info->fprintf_func(info->stream, "%-10s%p", name, concatp(xv, yv));
        break;
    case TCI_call8:
        assert(xi >= 8 && yi >= 8);
        if (TCG_TARGET_REG_BITS == 32) {
            info->fprintf_func(info->stream, "%-10s%s:%s=%p",
                               name, w, r, concatp(xv, yv));
            break;
        }
        /* FALLTHRU */
    case TCI_call4:
        assert(xi >= 8 && yi >= 8);
        info->fprintf_func(info->stream, "%-10s%s=%p",
                           name, r, concatp(xv, yv));
        break;

    case TCI_goto_tb:
        assert(xi >= 8 && yi >= 8);
        info->fprintf_func(info->stream, "%-10s[%p]", name, concatp(xv, yv));
        break;
    case TCI_goto_ptr:
        info->fprintf_func(info->stream, "%-10s%s", name, y);
        break;

    case TCI_setc:
        info->fprintf_func(info->stream, "%-10s%s", name, r);
        break;
    case TCI_mb:
        info->fprintf_func(info->stream, "%-10s", name);
        break;

    case TCI_st1:
    case TCI_st2:
    case TCI_st4:
    case TCI_st8:
        info->fprintf_func(info->stream, "%-10s[%s+%s]=%s", name, w, y, x);
        break;

    case TCI_ld1u:
    case TCI_ld1s:
    case TCI_ld2u:
    case TCI_ld2s:
    case TCI_ld4u:
    case TCI_ld4s:
    case TCI_ld8:
        info->fprintf_func(info->stream, "%-10s%s=[%s+%s]", name, r, w, y);
        break;

    case TCI_cmppeq:
    case TCI_cmppne:
    case TCI_cmpplt:
    case TCI_cmpple:
    case TCI_cmppgt:
    case TCI_cmppge:
    case TCI_cmppltu:
    case TCI_cmppleu:
    case TCI_cmppgtu:
    case TCI_cmppgeu:
        info->fprintf_func(info->stream, "%-10s%s:%s,%s:%s", name, w, r, x, y);
        break;

    case TCI_mulu2:
    case TCI_muls2:
        info->fprintf_func(info->stream, "%-10s%s:%s=%s,%s", name, w, r, x, y);
        break;

    case TCI_add2:
    case TCI_sub2:
        info->fprintf_func(info->stream, "%-10s%s:%s=%s:%s,%s:%s",
                           name, w, r, w, r, x, y);
        break;

    case TCI_invalid:
    default:
        info->fprintf_func(info->stream, "illegal opcode %d", opc);
    }

    return (addr - orig_addr) * (sizeof(*addr));
}

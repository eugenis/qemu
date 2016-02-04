/*
 * QEMU TILE-Gx helpers
 *
 *  Copyright (c) 2015 Chen Gang
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "cpu.h"
#include "qemu-common.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"
#include "internal.h"

/*
 * FSingle instructions implemenation:
 *
 * fsingle_add1         ; calc srca and srcb,
 *                      ; convert float_32 to TileGXFPSFmt result.
 *                      ; move TileGXFPSFmt result to dest.
 *
 * fsingle_sub1         ; calc srca and srcb.
 *                      ; convert float_32 to TileGXFPSFmt result.
 *                      ; move TileGXFPSFmt result to dest.
 *
 * fsingle_addsub2      ; nop.
 *
 * fsingle_mul1         ; calc srca and srcb.
 *                      ; convert float_32 value to TileGXFPSFmt result.
 *                      ; move TileGXFPSFmt result to dest.
 *
 * fsingle_mul2         ; move srca to dest.
 *
 * fsingle_pack1        ; nop
 *
 * fsingle_pack2        ; treate srca as TileGXFPSFmt result.
 *                      ; convert TileGXFPSFmt result to float_32 value.
 *                      ; move float_32 value to dest.
 */

#define GUARDBITS  4

static int get_f32_exp(uint32_t f)
{
    return extract32(f, 23, 8);
}

static uint32_t get_f32_man(uint32_t f)
{
    return extract32(f, 0, 23);
}

static int get_f32_sign(uint32_t f)
{
    return extract32(f, 31, 1);
}

static uint32_t make_f32(uint32_t sign, uint32_t exp, uint32_t man)
{
    return (sign << 31) | (exp << 23) | man;
}

static inline uint32_t get_fsingle_exp(uint64_t n)
{
    return n & 0xff;
}

static inline uint64_t set_fsingle_exp(uint64_t n, int exp)
{
    return deposit64(n, 0, 8, exp);
}

static inline uint32_t get_fsingle_sign(uint64_t n)
{
    return extract32(n, 10, 1);
}

static inline uint64_t set_fsingle_sign(uint64_t n, int s)
{
    return deposit64(n, 10, 1, s);
}

static inline uint32_t get_fsingle_man(uint64_t n)
{
    return extract64(n, 32, 32);
}

static inline uint64_t set_fsingle_man(uint64_t n, uint32_t man)
{
    return deposit64(n, 32, 32, man);
}

uint64_t helper_fsingle_pack2(CPUTLGState *env, uint64_t sfmt)
{
    uint32_t man = get_fsingle_man(sfmt);
    int exp = get_fsingle_exp(sfmt);

    if (exp == 0xff) {
        /* Inf and NaN, pre-encoded.  */
        man >>= GUARDBITS;
    } else if (man == 0) {
        /* Since we've excluded Inf, this must be 0.  */
        exp = 0;
    } else {
        /* Normalize, placing the implicit bit at bit 28.  */
        int shift = clz32(man) - 3;
        if (shift < 0) {
            man = (man >> -shift) | ((man << (shift & 31)) != 0);
            exp += -shift;
        } else if (shift > 0) {
            man <<= shift;
            exp -= shift;
        }

        /* Round to nearest, even.  */
        if ((man & ((1u << (GUARDBITS + 1)) - 1)) != (1 << (GUARDBITS - 1))) {
            man += 1 << (GUARDBITS - 1);
            /* Re-normalize if required.  */
            if (man & (1u << 29)) {
                man >>= 1;
                exp += 1;
            }
        }

        /* Check for overflow and underflow.  */
        if (exp >= 0xff) {
            /* Overflow to Inf.  */
            exp = 0xff;
            man = 0;
        } else if (exp < 1) {
            if (exp < -24) {
                /* Underflow to zero.  */
                man = 0;
            } else {
                /* Denormal result.  Clear and remove guard bits.  */
                man &= ~0xfu;
                man >>= GUARDBITS - exp;
            }
            exp = 0;
        } else {
            /* Normal result.  Remove guard bits and implicit bit.  */
            man = (man >> GUARDBITS) & 0x7fffff;
        }
    }

    return make_f32(get_fsingle_sign(sfmt), exp, man);
}

static uint64_t main_calc(uint32_t a, uint32_t b,
                          float32 (*calc)(float32, float32, float_status *))
{
    float_status fps = { .float_rounding_mode = float_round_nearest_even };
    float32 fa = make_float32(a);
    float32 fb = make_float32(b);

    /* Cheat and perform the entire operation in one go.  */
    uint32_t result = float32_val(calc(fa, fb, &fps));
    uint64_t sfmt;

    /* Format the result into the internal format.  */
    uint32_t exp = get_f32_exp(result);
    uint32_t man = get_f32_man(result);
    if (exp != 0 && exp != 0xff) {
        man |= 1u << 24;
    }
    man <<= GUARDBITS;

    sfmt = set_fsingle_man(0, man);
    sfmt = set_fsingle_exp(sfmt, exp);
    sfmt = set_fsingle_sign(sfmt, get_f32_sign(result));

    /* Compute comparison bits for the original inputs.  */
    if (float32_unordered(fa, fb, &fps)) {
        sfmt |= FSFD_FLAG_UN | FSFD_FLAG_NE;
    } else {
        sfmt |= (float32_eq(fa, fb, &fps)
                 ? FSFD_FLAG_EQ | FSFD_FLAG_LE | FSFD_FLAG_GE
                 : FSFD_FLAG_NE);
        sfmt |= (float32_lt(fa, fb, &fps)
                 ? FSFD_FLAG_LT | FSFD_FLAG_LE
                 : FSFD_FLAG_GE);
        if (!(sfmt & FSFD_FLAG_LE)) {
            sfmt |= FSFD_FLAG_GT;
        }
    }

    return sfmt;
}

uint64_t helper_fsingle_add1(CPUTLGState *env, uint64_t srca, uint64_t srcb)
{
    return main_calc(srca, srcb, float32_add);
}

uint64_t helper_fsingle_sub1(CPUTLGState *env, uint64_t srca, uint64_t srcb)
{
    return main_calc(srca, srcb, float32_sub);
}

uint64_t helper_fsingle_mul1(CPUTLGState *env, uint64_t srca, uint64_t srcb)
{
    return main_calc(srca, srcb, float32_mul);
}

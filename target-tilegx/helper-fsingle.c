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

#include "helper-fshared.c"

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

#define TILEGX_F_CALC_CVT   0     /* convert int to fsingle */
#define TILEGX_F_CALC_NCVT  1     /* Not convertion */

static uint32_t get_f32_exp(float32 f)
{
    return extract32(float32_val(f), 23, 8);
}

static void set_f32_exp(float32 *f, uint32_t exp)
{
    *f = make_float32(deposit32(float32_val(*f), 23, 8, exp));
}

static uint32_t get_f32_man(float32 f)
{
    return float32_val(f) & 0x7fffff;
}

static float32 create_f32_man(uint32_t man)
{
     return make_float32(man & 0x7fffff);
}

static inline uint32_t get_fsingle_exp(uint64_t n)
{
    return n & 0xff;
}

static inline uint64_t create_fsingle_exp(uint32_t exp)
{
    return exp & 0xff;
}

static inline uint32_t get_fsingle_sign(uint64_t n)
{
    return test_bit(10, &n);
}

static inline void set_fsingle_sign(uint64_t *n)
{
    set_bit(10, n);
}

static inline unsigned int get_fsingle_calc(uint64_t n)
{
    return test_bit(11, &n);
}

static inline void set_fsingle_calc(uint64_t *n, uint32_t calc)
{
    set_bit(11, n);
}

static inline unsigned int get_fsingle_man(uint64_t n)
{
    return n >> 32;
}

static inline uint64_t create_fsingle_man(uint32_t man)
{
    return (uint64_t)man << 32;
}

static uint64_t float32_to_sfmt(float32 f)
{
    uint64_t sfmt = 0;

    if (float32_is_neg(f)) {
        set_fsingle_sign(&sfmt);
    }
    sfmt |= create_fsingle_exp(get_f32_exp(f));
    sfmt |= create_fsingle_man((get_f32_man(f) << 8) | (1 << 31));

    return sfmt;
}

static float32 sfmt_to_float32(uint64_t sfmt, float_status *fp_status)
{
    float32 f;
    uint32_t sign = get_fsingle_sign(sfmt);
    uint32_t man = get_fsingle_man(sfmt);

    if (get_fsingle_calc(sfmt) == TILEGX_F_CALC_CVT) {
        if (sign) {
            return int32_to_float32(0 - man, fp_status);
        } else {
            return uint32_to_float32(man, fp_status);
        }
    } else {
        f = float32_set_sign(float32_zero, sign);
        f |= create_f32_man(man >> 8);
        set_f32_exp(&f, get_fsingle_exp(sfmt));
    }

    return f;
}

uint64_t helper_fsingle_pack2(CPUTLGState *env, uint64_t srca)
{
    return float32_val(sfmt_to_float32(srca, &env->fp_status));
}

static void ana_bits(float_status *fp_status,
                     float32 fsrca, float32 fsrcb, uint64_t *sfmt)
{
    if (float32_eq(fsrca, fsrcb, fp_status)) {
        *sfmt |= create_fsfd_flag_eq();
    } else {
        *sfmt |= create_fsfd_flag_ne();
    }

    if (float32_lt(fsrca, fsrcb, fp_status)) {
        *sfmt |= create_fsfd_flag_lt();
    }
    if (float32_le(fsrca, fsrcb, fp_status)) {
        *sfmt |= create_fsfd_flag_le();
    }

    if (float32_lt(fsrcb, fsrca, fp_status)) {
        *sfmt |= create_fsfd_flag_gt();
    }
    if (float32_le(fsrcb, fsrca, fp_status)) {
        *sfmt |= create_fsfd_flag_ge();
    }

    if (float32_unordered(fsrca, fsrcb, fp_status)) {
        *sfmt |= create_fsfd_flag_un();
    }
}

static uint64_t main_calc(float_status *fp_status,
                          float32 fsrca, float32 fsrcb,
                          float32 (*calc)(float32, float32, float_status *))
{
    uint64_t sfmt = float32_to_sfmt(calc(fsrca, fsrcb, fp_status));

    ana_bits(fp_status, fsrca, fsrcb, &sfmt);

    set_fsingle_calc(&sfmt, TILEGX_F_CALC_NCVT);
    return sfmt;
}

uint64_t helper_fsingle_add1(CPUTLGState *env, uint64_t srca, uint64_t srcb)
{
    return main_calc(&env->fp_status,
                     make_float32(srca), make_float32(srcb), float32_add);
}

uint64_t helper_fsingle_sub1(CPUTLGState *env, uint64_t srca, uint64_t srcb)
{
    return main_calc(&env->fp_status,
                     make_float32(srca), make_float32(srcb), float32_sub);
}

uint64_t helper_fsingle_mul1(CPUTLGState *env, uint64_t srca, uint64_t srcb)
{
    return main_calc(&env->fp_status,
                     make_float32(srca), make_float32(srcb), float32_mul);
}

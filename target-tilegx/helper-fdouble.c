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
 * FDouble instructions implemenation:
 *
 * fdouble_unpack_min   ; srca and srcb are float_64 value.
 *                      ; get the min absolute value's mantissa.
 *                      ; move "mantissa >> (exp_max - exp_min)" to dest.
 *
 * fdouble_unpack_max   ; srca and srcb are float_64 value.
 *                      ; get the max absolute value's mantissa.
 *                      ; move mantissa to dest.
 *
 * fdouble_add_flags    ; srca and srcb are float_64 value.
 *                      ; calc exp (exp_max), sign, and comp bits for flags.
 *                      ; set addsub bit to flags and move flags to dest.
 *
 * fdouble_sub_flags    ; srca and srcb are float_64 value.
 *                      ; calc exp (exp_max), sign, and comp bits for flags.
 *                      ; set addsub bit to flags and move flags to dest.
 *
 * fdouble_addsub:      ; dest, srca (max, min mantissa), and srcb (flags).
 *                      ; "dest +/- srca" depend on the add/sub bit of flags.
 *                      ; move result mantissa to dest.
 *
 * fdouble_mul_flags:   ; srca and srcb are float_64 value.
 *                      ; calc sign (xor), exp (min + max), and comp bits.
 *                      ; mix sign, exp, and comp bits as flags to dest.
 *
 * fdouble_pack1        ; move srcb (flags) to dest.
 *
 * fdouble_pack2        ; srca, srcb (high, low mantissa), and dest (flags)
 *                      ; normalize and pack result from srca, srcb, and dest.
 *                      ; move result to dest.
 */

#define TILEGX_F_EXP_DZERO  0x3ff /* Zero exp for double 11-bits */
#define TILEGX_F_EXP_DMAX   0x7fe /* max exp for double 11-bits */
#define TILEGX_F_EXP_DUF    0x1000/* underflow exp bit for double */

#define TILEGX_F_MAN_HBIT   (1ULL << 59)

#define TILEGX_F_CALC_ADD   1     /* Perform absolute add operation */
#define TILEGX_F_CALC_SUB   2     /* Perform absolute sub operation */
#define TILEGX_F_CALC_MUL   3     /* Perform absolute mul operation */

static uint32_t get_f64_exp(float64 d)
{
    return extract64(float64_val(d), 52, 11);
}

static void set_f64_exp(float64 *d, uint32_t exp)
{
    *d = make_float64(deposit64(float64_val(*d), 52, 11, exp));
}

static uint64_t get_f64_man(float64 d)
{
    return extract64(float64_val(d), 0, 52);
}

static uint64_t fr_to_man(float64 d)
{
    uint64_t val = get_f64_man(d) << 7;

    if (get_f64_exp(d)) {
        val |= TILEGX_F_MAN_HBIT;
    }

    return val;
}

static uint64_t get_fdouble_man(uint64_t n)
{
    return extract64(n, 0, 60);
}

static void set_fdouble_man(uint64_t *n, uint64_t man)
{
    *n = deposit64(*n, 0, 60, man);
}

static uint64_t get_fdouble_man_of(uint64_t n)
{
    return test_bit(60, &n);
}

static void clear_fdouble_man_of(uint64_t *n)
{
    return clear_bit(60, n);
}

static uint32_t get_fdouble_nan(uint64_t n)
{
    return test_bit(24, &n);
}

static void set_fdouble_nan(uint64_t *n)
{
    set_bit(24, n);
}

static uint32_t get_fdouble_inf(uint64_t n)
{
    return test_bit(23, &n);
}

static void set_fdouble_inf(uint64_t *n)
{
    set_bit(23, n);
}

static uint32_t get_fdouble_calc(uint64_t n)
{
    return extract32(n, 21, 2);
}

static void set_fdouble_calc(uint64_t *n, uint32_t calc)
{
    *n = deposit64(*n, 21, 2, calc);
}

static uint32_t get_fdouble_sign(uint64_t n)
{
    return test_bit(20, &n);
}

static void set_fdouble_sign(uint64_t *n)
{
    set_bit(20, n);
}

static uint32_t get_fdouble_vexp(uint64_t n)
{
    return extract32(n, 7, 13);
}

static void set_fdouble_vexp(uint64_t *n, uint32_t vexp)
{
    *n = deposit64(*n, 7, 13, vexp);
}

uint64_t helper_fdouble_unpack_min(CPUTLGState *env,
                                   uint64_t srca, uint64_t srcb)
{
    uint64_t v = 0;
    uint32_t expa = get_f64_exp(srca);
    uint32_t expb = get_f64_exp(srcb);

    if (float64_is_any_nan(srca) || float64_is_any_nan(srcb)
        || float64_is_infinity(srca) || float64_is_infinity(srcb)) {
        return 0;
    } else if (expa > expb) {
        if (expa - expb < 64) {
            set_fdouble_man(&v, fr_to_man(srcb) >> (expa - expb));
        } else {
            return 0;
        }
    } else if (expa < expb) {
        if (expb - expa < 64) {
            set_fdouble_man(&v, fr_to_man(srca) >> (expb - expa));
        } else {
            return 0;
        }
    } else if (get_f64_man(srca) > get_f64_man(srcb)) {
        set_fdouble_man(&v, fr_to_man(srcb));
    } else {
        set_fdouble_man(&v, fr_to_man(srca));
    }

    return v;
}

uint64_t helper_fdouble_unpack_max(CPUTLGState *env,
                                   uint64_t srca, uint64_t srcb)
{
    uint64_t v = 0;
    uint32_t expa = get_f64_exp(srca);
    uint32_t expb = get_f64_exp(srcb);

    if (float64_is_any_nan(srca) || float64_is_any_nan(srcb)
        || float64_is_infinity(srca) || float64_is_infinity(srcb)) {
        return 0;
    } else if (expa > expb) {
        set_fdouble_man(&v, fr_to_man(srca));
    } else if (expa < expb) {
        set_fdouble_man(&v, fr_to_man(srcb));
    } else if (get_f64_man(srca) > get_f64_man(srcb)) {
        set_fdouble_man(&v, fr_to_man(srca));
    } else {
        set_fdouble_man(&v, fr_to_man(srcb));
    }

    return v;
}

uint64_t helper_fdouble_addsub(CPUTLGState *env,
                               uint64_t dest, uint64_t srca, uint64_t srcb)
{
    if (get_fdouble_calc(srcb) == TILEGX_F_CALC_ADD) {
        return dest + srca; /* maybe set addsub overflow bit */
    } else {
        return dest - srca;
    }
}

/* absolute-add/mul may cause add/mul carry or overflow */
static bool proc_oflow(uint64_t *flags, uint64_t *v, uint64_t *srcb)
{
    if (get_fdouble_man_of(*v)) {
        set_fdouble_vexp(flags, get_fdouble_vexp(*flags) + 1);
        *srcb >>= 1;
        *srcb |= *v << 63;
        *v >>= 1;
        clear_fdouble_man_of(v);
    }
    return get_fdouble_vexp(*flags) > TILEGX_F_EXP_DMAX;
}

uint64_t helper_fdouble_pack2(CPUTLGState *env, uint64_t flags /* dest */,
                              uint64_t srca, uint64_t srcb)
{
    uint64_t v = srca;
    float64 d = float64_set_sign(float64_zero, get_fdouble_sign(flags));

    /*
     * fdouble_add_flags, fdouble_sub_flags, or fdouble_mul_flags have
     * processed exceptions. So need not process fp_status, again.
     */

    if (get_fdouble_nan(flags)) {
        return float64_val(float64_default_nan);
    } else if (get_fdouble_inf(flags)) {
        return float64_val(d |= float64_infinity);
    }

    /* absolute-mul needs left shift 4 + 1 bytes to match the real mantissa */
    if (get_fdouble_calc(flags) == TILEGX_F_CALC_MUL) {
        v <<= 5;
        v |= srcb >> 59;
        srcb <<= 5;
    }

    /* must check underflow, firstly */
    if (get_fdouble_vexp(flags) & TILEGX_F_EXP_DUF) {
        return float64_val(d);
    }

    if (proc_oflow(&flags, &v, &srcb)) {
        return float64_val(d |= float64_infinity);
    }

    while (!(get_fdouble_man(v) & TILEGX_F_MAN_HBIT)
           && (get_fdouble_man(v) | srcb)) {
        set_fdouble_vexp(&flags, get_fdouble_vexp(flags) - 1);
        set_fdouble_man(&v, get_fdouble_man(v) << 1);
        set_fdouble_man(&v, get_fdouble_man(v) | (srcb >> 63));
        srcb <<= 1;
    }

    /* check underflow, again, after format */
    if ((get_fdouble_vexp(flags) & TILEGX_F_EXP_DUF) || !get_fdouble_man(v)) {
        return float64_val(d);
    }

    if (get_fdouble_sign(flags)) {
        d = int64_to_float64(0 - get_fdouble_man(v), &env->fp_status);
    } else {
        d = uint64_to_float64(get_fdouble_man(v), &env->fp_status);
    }

    if (get_f64_exp(d) == 59 + TILEGX_F_EXP_DZERO) {
        set_f64_exp(&d, get_fdouble_vexp(flags));
    } else {                            /* for carry and overflow again */
        set_f64_exp(&d, get_fdouble_vexp(flags) + 1);
        if (get_f64_exp(d) == TILEGX_F_EXP_DMAX) {
            d = float64_infinity;
        }
    }

    d = float64_set_sign(d, get_fdouble_sign(flags));

    return float64_val(d);
}

static void ana_bits(float_status *fp_status,
                     float64 fsrca, float64 fsrcb, uint64_t *dfmt)
{
    if (float64_eq(fsrca, fsrcb, fp_status)) {
        *dfmt |= create_fsfd_flag_eq();
    } else {
        *dfmt |= create_fsfd_flag_ne();
    }

    if (float64_lt(fsrca, fsrcb, fp_status)) {
        *dfmt |= create_fsfd_flag_lt();
    }
    if (float64_le(fsrca, fsrcb, fp_status)) {
        *dfmt |= create_fsfd_flag_le();
    }

    if (float64_lt(fsrcb, fsrca, fp_status)) {
        *dfmt |= create_fsfd_flag_gt();
    }
    if (float64_le(fsrcb, fsrca, fp_status)) {
        *dfmt |= create_fsfd_flag_ge();
    }

    if (float64_unordered(fsrca, fsrcb, fp_status)) {
        *dfmt |= create_fsfd_flag_un();
    }
}

static uint64_t main_calc(float_status *fp_status,
                          float64 fsrca, float64 fsrcb,
                          float64 (*calc)(float64, float64, float_status *))
{
    float64 d;
    uint64_t flags = 0;
    uint32_t expa = get_f64_exp(fsrca);
    uint32_t expb = get_f64_exp(fsrcb);

    ana_bits(fp_status, fsrca, fsrcb, &flags);

    d = calc(fsrca, fsrcb, fp_status); /* also check exceptions */
    if (float64_is_neg(d)) {
        set_fdouble_sign(&flags);
    }

    if (float64_is_any_nan(d)) {
        set_fdouble_nan(&flags);
    } else if (float64_is_infinity(d)) {
        set_fdouble_inf(&flags);
    } else if (calc == float64_add) {
        set_fdouble_vexp(&flags, (expa > expb) ? expa : expb);
        set_fdouble_calc(&flags,
                         (float64_is_neg(fsrca) == float64_is_neg(fsrcb))
                             ? TILEGX_F_CALC_ADD : TILEGX_F_CALC_SUB);

    } else if (calc == float64_sub) {
        set_fdouble_vexp(&flags, (expa > expb) ? expa : expb);
        set_fdouble_calc(&flags,
                         (float64_is_neg(fsrca) != float64_is_neg(fsrcb))
                             ? TILEGX_F_CALC_ADD : TILEGX_F_CALC_SUB);

    } else {
        set_fdouble_vexp(&flags, (int64_t)(expa - TILEGX_F_EXP_DZERO)
                                 + (int64_t)(expb - TILEGX_F_EXP_DZERO)
                                 + TILEGX_F_EXP_DZERO);
        set_fdouble_calc(&flags, TILEGX_F_CALC_MUL);
    }

    return flags;
}

uint64_t helper_fdouble_add_flags(CPUTLGState *env,
                                  uint64_t srca, uint64_t srcb)
{
    return main_calc(&env->fp_status,
                     make_float64(srca), make_float64(srcb), float64_add);
}

uint64_t helper_fdouble_sub_flags(CPUTLGState *env,
                                  uint64_t srca, uint64_t srcb)
{
    return main_calc(&env->fp_status,
                     make_float64(srca), make_float64(srcb), float64_sub);
}

uint64_t helper_fdouble_mul_flags(CPUTLGState *env,
                                  uint64_t srca, uint64_t srcb)
{
    return main_calc(&env->fp_status,
                     make_float64(srca), make_float64(srcb), float64_mul);
}

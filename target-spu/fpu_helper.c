/*
 *  Synergistic Processor Unit (SPU) emulation
 *  FPU helper functions.
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
#include "fpu/softfloat.h"
#include "exec/helper-proto.h"


/* ??? The SPU floating point unit is majorly restricted, particularly when
   it comes to single precision.  We do not attempt to model that yet.  */

#define HELPER_SP2(NAME, FUNC)                                          \
void helper_##NAME(CPUSPUState *env, uint32_t args)                     \
{                                                                       \
    uint32_t ra = args & 0xff, rb = args >> 8, i;                       \
    for (i = 0; i < 4; ++i) {                                           \
        env->ret[i] = FUNC(env->gpr[ra * 4 + i], env->gpr[rb * 4 + i],  \
                           &env->sp_status[i]);                         \
    }                                                                   \
}

#define HELPER_SP1(NAME, FUNC)                                          \
void helper_##NAME(CPUSPUState *env, uint32_t ra)                       \
{                                                                       \
    uint32_t i;                                                         \
    for (i = 0; i < 4; ++i) {                                           \
        env->ret[i] = FUNC(env->gpr[ra * 4 + i], &env->sp_status[i]);   \
    }                                                                   \
}

#define HELPER_SP_FMA(NAME, O)                                          \
void helper_##NAME(CPUSPUState *env, uint32_t args)                     \
{                                                                       \
    uint32_t ra = args & 0xff, rb = (args >> 8) & 0xff, rc = args >> 16, i; \
    for (i = 0; i < 4; ++i) {                                           \
        env->ret[i] = float32_muladd(env->gpr[ra * 4 + i],              \
                                     env->gpr[rb * 4 + i],              \
                                     env->gpr[rc * 4 + i], O,           \
                                     &env->sp_status[i]);               \
    }                                                                   \
}

HELPER_SP2(fa, float32_add)
HELPER_SP2(fs, float32_sub)
HELPER_SP2(fm, float32_mul)

HELPER_SP_FMA(fma, 0)
HELPER_SP_FMA(fnms, float_muladd_negate_product)
HELPER_SP_FMA(fms, float_muladd_negate_c)

/* ??? This isn't really right, as we're ignoring the step portion.  */
static inline float32 do_frest(float32 a, float_status *stat)
{
    return float32_div(float32_one, a, stat);
}

/* ??? Likewise.  */
static inline float32 do_frsqest(float32 a, float_status *stat)
{
    a = float32_abs(a);
    a = float32_sqrt(a, stat);
    return float32_div(float32_one, a, stat);
}

/* ??? This is even more wrong, but works with the above mistakes,
   given that we produced exact results.  */
static inline float32 do_fi(float32 a, float32 b, float_status *stat)
{
    return b;
}

HELPER_SP1(frest, do_frest)
HELPER_SP1(frsqest, do_frsqest)
HELPER_SP2(fi, do_fi)

static inline void sd(uint32_t *base, int lane, float64 v)
{
    base[lane * 2 + 0] = v >> 32;
    base[lane * 2 + 1] = v;
}

static inline float64 gd(uint32_t *base, int lane)
{
    return (uint64_t)base[lane * 2] << 32 | base[lane * 2 + 1];
}

#define HELPER_DP(NAME, FUNC)                                           \
void helper_##NAME(CPUSPUState *env, uint32_t args)                     \
{                                                                       \
    uint32_t ra = args & 0xff, rb = args >> 8, i;                       \
    for (i = 0; i < 2; ++i) {                                           \
        sd(env->ret, i, float64_##FUNC(gd(&env->gpr[ra * 4], i),        \
                                       gd(&env->gpr[rb * 4], i),        \
                                       &env->dp_status[i]));            \
    }                                                                   \
}

HELPER_DP(dfa, add)
HELPER_DP(dfs, sub)
HELPER_DP(dfm, mul)

#define HELPER_DP_FMA(NAME, O)                                          \
void helper_##NAME(CPUSPUState *env, uint32_t args)                     \
{                                                                       \
    uint32_t ra = args & 0xff, rb = (args >> 8) & 0xff, rc = args >> 16, i; \
    for (i = 0; i < 2; ++i) {                                           \
        sd(env->ret, i, float64_muladd(gd(&env->gpr[ra * 4], i),        \
                                       gd(&env->gpr[rb * 4], i),        \
                                       gd(&env->gpr[rc * 4], i),        \
                                       O, &env->dp_status[i]));         \
    }                                                                   \
}

HELPER_DP_FMA(dfma, 0)
HELPER_DP_FMA(dfnma, float_muladd_negate_result)
HELPER_DP_FMA(dfms, float_muladd_negate_c)
HELPER_DP_FMA(dfnms, float_muladd_negate_c | float_muladd_negate_result)

void helper_csflt(CPUSPUState *env, uint32_t ra, uint32_t scale)
{
    uint32_t i;
    float32 t;
    for (i = 0; i < 4; ++i) {
        t = int32_to_float32(env->gpr[ra * 4 + i], &env->sp_status[i]);
        t = float32_scalbn(t, -scale, &env->sp_status[i]);
        env->ret[i] = t;
    }
}

void helper_cuflt(CPUSPUState *env, uint32_t ra, uint32_t scale)
{
    uint32_t i;
    float32 t;
    for (i = 0; i < 4; ++i) {
        t = uint32_to_float32(env->gpr[ra * 4 + i], &env->sp_status[i]);
        t = float32_scalbn(t, -scale, &env->sp_status[i]);
        env->ret[i] = t;
    }
}

void helper_cflts(CPUSPUState *env, uint32_t ra, uint32_t scale)
{
    uint32_t i;
    float32 t;
    for (i = 0; i < 4; ++i) {
        t = float32_scalbn(env->gpr[ra * 4 + i], scale, &env->sp_status[i]);
        t = float32_to_int32(t, &env->sp_status[i]);
        env->ret[i] = t;
    }
}

void helper_cfltu(CPUSPUState *env, uint32_t ra, uint32_t scale)
{
    uint32_t i;
    float32 t;
    for (i = 0; i < 4; ++i) {
        t = float32_scalbn(env->gpr[ra * 4 + i], scale, &env->sp_status[i]);
        t = float32_to_uint32(t, &env->sp_status[i]);
        env->ret[i] = t;
    }
}

void helper_frds(CPUSPUState *env, uint32_t ra)
{
    uint32_t i;
    for (i = 0; i < 2; ++i) {
        env->ret[i * 2] = float64_to_float32(gd(&env->gpr[ra * 4], i),
                                             &env->dp_status[i]);
        env->ret[i * 2 + 1] = 0;
    }
}

void helper_fesd(CPUSPUState *env, uint32_t ra)
{
    uint32_t i;
    for (i = 0; i < 2; ++i) {
        sd(env->ret, i, float32_to_float64(env->gpr[ra * 4 + i * 2],
                                           &env->sp_status[i * 2]));
    }
}

static inline uint32_t float32_ceq(float32 a, float32 b, float_status *s)
{
    return float32_eq_quiet(a, b, s) ? -1 : 0;
}

static inline uint32_t float32_cmeq(float32 a, float32 b, float_status *s)
{
    return float32_eq_quiet(float32_abs(a), float32_abs(b), s) ? -1 : 0;
}

static inline uint32_t float32_cgt(float32 a, float32 b, float_status *s)
{
    return float32_lt_quiet(b, a, s) ? -1 : 0;
}

static inline uint32_t float32_cmgt(float32 a, float32 b, float_status *s)
{
    return float32_lt_quiet(float32_abs(b), float32_abs(a), s) ? -1 : 0;
}

HELPER_SP2(fceq, float32_ceq)
HELPER_SP2(fcmeq, float32_cmeq)
HELPER_SP2(fcgt, float32_cgt)
HELPER_SP2(fcmgt, float32_cmgt)

static inline uint64_t float64_ceq(float64 a, float64 b, float_status *s)
{
    return float64_eq_quiet(a, b, s) ? -1 : 0;
}

static inline uint64_t float64_cmeq(float64 a, float64 b, float_status *s)
{
    return float64_eq_quiet(float64_abs(a), float64_abs(b), s) ? -1 : 0;
}

static inline uint64_t float64_cgt(float64 a, float64 b, float_status *s)
{
    return float64_lt_quiet(b, a, s) ? -1 : 0;
}

static inline uint64_t float64_cmgt(float64 a, float64 b, float_status *s)
{
    return float64_lt_quiet(float64_abs(b), float64_abs(a), s) ? -1 : 0;
}

HELPER_DP(dfceq, ceq)
HELPER_DP(dfcmeq, cmeq)
HELPER_DP(dfcgt, cgt)
HELPER_DP(dfcmgt, cmgt)

uint32_t helper_dftsv(uint64_t a, uint32_t mask)
{
    bool test = false;

    if (mask & 0x40) {
        test |= float64_is_any_nan(a);
    }
    mask &= (float64_is_neg(a) ? 0x15 : 0x2a);
    if (mask & 0x30) {
        test |= float64_is_infinity(a);
    }
    if (mask & 0x0f) {
        if (float64_is_zero(a)) {
            test |= ((mask & 0x0c) != 0);
        } else if (float64_is_zero_or_denormal(a)) {
            test |= ((mask & 0x03) != 0);
        }
    }

    return test ? -1 : 0;
}

/* Extract using the big-endian bit numbering used by SPU.  */
static inline uint32_t bextract(uint32_t w, int pos, int len)
{
    return extract32(w, 32 - pos - len, len);
}

/* Insert using the big-endian bit numbering used by SPU.  */
static inline uint32_t bdeposit(uint32_t w, int pos, int len, int v)
{
    return deposit32(w, 32 - pos - len, len, v);
}

static inline int spu_to_round(int x)
{
    switch (x) {
    default:
        return float_round_nearest_even;
    case 1:
        return float_round_to_zero;
    case 2:
        return float_round_up;
    case 3:
        return float_round_down;
    }
}

static inline int round_to_spu(int x)
{
    switch (x) {
    case float_round_nearest_even:
        return 0;
    case float_round_to_zero:
        return 1;
    case float_round_up:
        return 2;
    case float_round_down:
        return 3;
    default:
        g_assert_not_reached();
    }
}

static inline int spspu_to_flag(int x)
{
    int r = 0;
    r |= (x & 4 ? float_flag_overflow : 0);
    r |= (x & 2 ? float_flag_underflow : 0);
    /* ??? diff bit */
    return r;
}

static inline int flag_to_spspu(int x)
{
    int r = 0;
    r |= (x & float_flag_overflow ? 4 : 0);
    r |= (x & float_flag_underflow ? 2 : 0);
    /* ??? diff bit */
    return r;
}

static inline int dpspu_to_flag(int x)
{
    int r = 0;
    r |= (x & 32 ? float_flag_overflow : 0);
    r |= (x & 16 ? float_flag_underflow : 0);
    r |= (x & 8 ? float_flag_inexact : 0);
    r |= (x & 4 ? float_flag_invalid : 0);
    /* ??? qnan diff bit */
    /* ??? denorm diff bit */
    return r;
}

static inline int flag_to_dpspu(int x)
{
    int r = 0;
    r |= (x & float_flag_overflow ? 32 : 0);
    r |= (x & float_flag_underflow ? 16 : 0);
    r |= (x & float_flag_inexact ? 8 : 0);
    r |= (x & float_flag_invalid ? 4 : 0);
    /* ??? qnan diff bit */
    /* ??? denorm diff bit */
    return r;
}

void helper_fscrwr(CPUSPUState *env, uint32_t a0, uint32_t a1,
                   uint32_t a2, uint32_t a3)
{
    int sf0, sf1, sf2, sf3;
    int x;

    x = spu_to_round(bextract(a0, 20, 2));
    set_float_rounding_mode(x, &env->dp_status[0]);
    x = spu_to_round(bextract(a0, 22, 2));
    set_float_rounding_mode(x, &env->dp_status[1]);
    x = dpspu_to_flag(bextract(a1, 50 % 32, 6));
    set_float_exception_flags(x, &env->dp_status[0]);
    x = dpspu_to_flag(bextract(a2, 82 % 32, 6));
    set_float_exception_flags(x, &env->dp_status[1]);

    sf0 = spspu_to_flag(bextract(a0, 29, 3));
    sf1 = spspu_to_flag(bextract(a1, 61 % 32, 3));
    sf2 = spspu_to_flag(bextract(a2, 93 % 32, 3));
    sf3 = spspu_to_flag(bextract(a3, 125 % 32, 3));
    if (bextract(a3, 116 % 32, 1)) {
        sf0 |= float_flag_divbyzero;
    }
    if (bextract(a3, 117 % 32, 1)) {
        sf1 |= float_flag_divbyzero;
    }
    if (bextract(a3, 118 % 32, 1)) {
        sf2 |= float_flag_divbyzero;
    }
    if (bextract(a3, 119 % 32, 1)) {
        sf3 |= float_flag_divbyzero;
    }
    set_float_exception_flags(sf0, &env->sp_status[0]);
    set_float_exception_flags(sf1, &env->sp_status[1]);
    set_float_exception_flags(sf2, &env->sp_status[2]);
    set_float_exception_flags(sf3, &env->sp_status[3]);
}

void helper_fscrrd(CPUSPUState *env)
{
    uint32_t r0 = 0, r1 = 0, r2 = 0, r3 = 0;
    int x;

    x = get_float_rounding_mode(&env->dp_status[0]);
    r0 = bdeposit(r0, 20, 2, round_to_spu(x));
    x = get_float_rounding_mode(&env->dp_status[1]);
    r0 = bdeposit(r0, 22, 2, round_to_spu(x));

    x = get_float_exception_flags(&env->dp_status[0]);
    r1 = bdeposit(r1, 50 % 32, 6, flag_to_dpspu(x));
    x = get_float_exception_flags(&env->dp_status[1]);
    r2 = bdeposit(r2, 82 % 32, 6, flag_to_dpspu(x));

    x = get_float_exception_flags(&env->sp_status[0]);
    r0 = bdeposit(r0, 29, 2, flag_to_spspu(x));
    if (x & float_flag_divbyzero) {
        r3 = bdeposit(r3, 116, 1, 1);
    }

    x = get_float_exception_flags(&env->sp_status[1]);
    r1 = bdeposit(r1, 61 % 32, 2, flag_to_spspu(x));
    if (x & float_flag_divbyzero) {
        r3 = bdeposit(r3, 117, 1, 1);
    }

    x = get_float_exception_flags(&env->sp_status[2]);
    r2 = bdeposit(r2, 93 % 32, 2, flag_to_spspu(x));
    if (x & float_flag_divbyzero) {
        r3 = bdeposit(r3, 118, 1, 1);
    }

    x = get_float_exception_flags(&env->sp_status[3]);
    r3 = bdeposit(r3, 125 % 32, 2, flag_to_spspu(x));
    if (x & float_flag_divbyzero) {
        r3 = bdeposit(r3, 119, 1, 1);
    }

    env->ret[0] = r0;
    env->ret[1] = r1;
    env->ret[2] = r2;
    env->ret[3] = r3;
}

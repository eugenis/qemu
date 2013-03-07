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

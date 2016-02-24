/*
 *  Vector Floating Point Support
 *
 *  Copyright (c) 2005 Fabrice Bellard
 *  Copyright (c) 2008 Intel Corporation  <andrew.zaborowski@intel.com>
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
#include "exec/helper-proto.h"
#include "qemu/host-utils.h"

#define HELPERS_1(NAME, F)                                  \
uint32_t helper_ss_ ## NAME(CPUX86State *env, uint32_t a)   \
{                                                           \
    return float32_##F(a, &env->sse_status);                \
}                                                           \
uint64_t helper_ps_ ## NAME(CPUX86State *env, uint64_t a)   \
{                                                           \
    uint32_t rl = float32_##F(a, &env->sse_status);         \
    uint32_t rh = float32_##F(a >> 32, &env->sse_status);   \
    return ((uint64_t)rh << 32) | rl;                       \
}                                                           \
uint64_t helper_d_## NAME(CPUX86State *env, uint64_t a)     \
{                                                           \
    return float64_##F(a, &env->sse_status);                \
}

#define HELPERS_2(NAME, F)                                              \
uint32_t helper_ss_ ## NAME(CPUX86State *env, uint32_t a, uint32_t b)   \
{                                                                       \
    return float32_##F(a, b, &env->sse_status);                         \
}                                                                       \
uint64_t helper_ps_ ## NAME(CPUX86State *env, uint64_t a, uint64_t b)   \
{                                                                       \
    uint32_t rl = float32_##F(a, b, &env->sse_status);                  \
    uint32_t rh = float32_##F(a >> 32, b >> 32, &env->sse_status);      \
    return ((uint64_t)rh << 32) | rl;                                   \
}                                                                       \
uint64_t helper_d_## NAME(CPUX86State *env, uint64_t a, uint64_t b)     \
{                                                                       \
    return float64_##F(a, b, &env->sse_status);                         \
}


HELPERS_2(add, add)
HELPERS_2(sub, sub)
HELPERS_2(mul, mul)
HELPERS_2(div, div)
HELPERS_1(sqrt, sqrt)

/* Note that the choice of comparison op here is important to get the
 * special cases right: for min and max Intel specifies that (-0,0),
 * (NaN, anything) and (anything, NaN) return the second argument.
 */
static inline uint32_t float32_ssemin(uint32_t a, uint32_t b, float_status *s)
{
    return float32_lt(a, b, s) ? a : b;
}

static inline uint64_t float64_ssemin(uint64_t a, uint64_t b, float_status *s)
{
    return float64_lt(a, b, s) ? a : b;
}

static inline uint32_t float32_ssemax(uint32_t a, uint32_t b, float_status *s)
{
    return float32_lt(b, a, s) ? a : b;
}

static inline uint64_t float64_ssemax(uint64_t a, uint64_t b, float_status *s)
{
    return float64_lt(b, a, s) ? a : b;
}

HELPERS_2(min, ssemin)
HELPERS_2(max, ssemax)

static const uint8_t comis_eflags[4] = {CC_C, CC_Z, 0, CC_Z | CC_P | CC_C};

target_ulong helper_ss_comi(CPUX86State *env, uint32_t a, uint32_t b)
{
    intptr_t ret = float32_compare(a, b, &env->sse_status);
    return comis_eflags[ret + 1];
}

target_ulong helper_d_comi(CPUX86State *env, uint64_t a, uint64_t b)
{
    intptr_t ret = float64_compare(a, b, &env->sse_status);
    return comis_eflags[ret + 1];
}

target_ulong helper_ss_ucomi(CPUX86State *env, uint32_t a, uint32_t b)
{
    intptr_t ret = float32_compare_quiet(a, b, &env->sse_status);
    return comis_eflags[ret + 1];
}

target_ulong helper_d_ucomi(CPUX86State *env, uint64_t a, uint64_t b)
{
    intptr_t ret = float64_compare_quiet(a, b, &env->sse_status);
    return comis_eflags[ret + 1];
}

uint32_t helper_cvtd2s(CPUX86State *env, uint64_t a)
{
    return float64_to_float32(a, &env->sse_status);
}

uint64_t helper_cvts2d(CPUX86State *env, uint32_t a)
{
    return float32_to_float64(a, &env->sse_status);
}

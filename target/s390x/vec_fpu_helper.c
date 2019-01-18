/*
 * QEMU TCG support -- s390x vector floating point instruction support
 *
 * Copyright (C) 2019 Red Hat Inc
 *
 * Authors:
 *   David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu-common.h"
#include "cpu.h"
#include "internal.h"
#include "vec.h"
#include "tcg_s390x.h"
#include "tcg/tcg-gvec-desc.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "fpu/softfloat.h"

#define VIC_INVALID         0x1
#define VIC_DIVBYZERO       0x2
#define VIC_OVERFLOW        0x3
#define VIC_UNDERFLOW       0x4
#define VIC_INEXACT         0x5

/* returns the VEX. If the VEX is 0, there is no trap */
static uint8_t check_ieee_exc(CPUS390XState *env, uint8_t enr, bool XxC,
                              uint8_t *vec_exc)
{
    uint8_t vece_exc = 0 ,trap_exc;
    unsigned qemu_exc;

    /* Retrieve and clear the softfloat exceptions */
    qemu_exc = env->fpu_status.float_exception_flags;
    if (qemu_exc == 0) {
        return 0;
    }
    env->fpu_status.float_exception_flags = 0;

    vece_exc = s390_softfloat_exc_to_ieee(qemu_exc);

    /* Add them to the vector-wide s390x exception bits */
    *vec_exc |= vece_exc;

    /* Check for traps and construct the VXC */
    trap_exc = vece_exc & env->fpc >> 24;
    if (trap_exc) {
        if (trap_exc & S390_IEEE_MASK_INVALID) {
            return enr << 4 | VIC_INVALID;
        } else if (trap_exc & S390_IEEE_MASK_DIVBYZERO) {
            return enr << 4 | VIC_DIVBYZERO;
        } else if (trap_exc & S390_IEEE_MASK_OVERFLOW) {
            return enr << 4 | VIC_OVERFLOW;
        } else if (trap_exc & S390_IEEE_MASK_UNDERFLOW) {
            return enr << 4 | VIC_UNDERFLOW;
        } else if (!XxC) {
            g_assert(trap_exc & S390_IEEE_MASK_INEXACT);
            /* inexact has lowest priority on traps */
            return enr << 4 | VIC_INEXACT;
        }
    }
    return 0;
}

static void handle_ieee_exc(CPUS390XState *env, uint8_t vxc, uint8_t vec_exc,
                            uintptr_t retaddr)
{
    if (vxc) {
        /* on traps, the fpc flags are not updated, instruction is suppressed */
        tcg_s390_vector_exception(env, vxc, retaddr);
    }
    if (vec_exc) {
        /* indicate exceptions for all elements combined */
        env->fpc |= vec_exc << 16;
    }
}

typedef float64 (*vf2op64_fn)(float64 a, float64 b, float_status *s);
static void gvec_vf2op64(void *v1, const void *v2, const void *v3,
                         CPUS390XState *env, uint32_t desc, vf2op64_fn fn)
{
    const bool s = extract32(simd_data(desc), 3, 1);
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i;

    for (i = 0; i < 2; i++) {
        const float64 a = make_float64(s390_vec_read_element64(v2, i));
        const float64 b = make_float64(s390_vec_read_element64(v3, i));
        const uint64_t ret = float64_val(fn(a, b, &env->fpu_status));

        s390_vec_write_element64(&tmp, i, ret);
        vxc = check_ieee_exc(env, i, false, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    handle_ieee_exc(env, vxc, vec_exc, GETPC());
    *(S390Vector *)v1 = tmp;
}

void HELPER(gvec_vfa64)(void *v1, const void *v2, const void *v3,
                        CPUS390XState *env, uint32_t desc)
{
    gvec_vf2op64(v1, v2, v3, env, desc, float64_add);
}

static void gvec_wf64(const void *v1, const void *v2, CPUS390XState *env,
                      bool signal)
{
    /* only the zero-indexed elements are compared */
    const float64 a = make_float64(s390_vec_read_element64(v1, 0));
    const float64 b = make_float64(s390_vec_read_element64(v2, 0));
    uint8_t vxc, vec_exc = 0;
    int cmp;

    if (signal) {
        cmp = float64_compare(a, b, &env->fpu_status);
    } else {
        cmp = float64_compare_quiet(a, b, &env->fpu_status);
    }
    vxc = check_ieee_exc(env, 0, false, &vec_exc);
    handle_ieee_exc(env, vxc, vec_exc, GETPC());

    env->cc_op = float_comp_to_cc(env, cmp);
}

void HELPER(gvec_wfc64)(const void *v1, const void *v2, CPUS390XState *env,
                        uint32_t desc)
{
    gvec_wf64(v1, v2, env, false);
}

void HELPER(gvec_wfk64)(const void *v1, const void *v2, CPUS390XState *env,
                        uint32_t desc)
{
    gvec_wf64(v1, v2, env, true);
}

static void gvec_vfc64(void *v1, const void *v2, const void *v3,
                       CPUS390XState *env, uint32_t desc, bool test_equal,
                       bool test_high)
{
    const bool set_cc = extract32(simd_data(desc), 4, 1);
    const bool s = extract32(simd_data(desc), 3, 1);
    uint8_t vxc, vec_exc = 0;
    int match = 0, no_match = 0;
    S390Vector tmp = {};
    int i;

    for (i = 0; i < 2; i++) {
        const float64 a = make_float64(s390_vec_read_element64(v2, i));
        const float64 b = make_float64(s390_vec_read_element64(v3, i));
        const int cmp = float64_compare_quiet(a, b, &env->fpu_status);

        if ((cmp == float_relation_equal && test_equal) ||
            (cmp == float_relation_greater && test_high)){
            match++;
            s390_vec_write_element64(&tmp, i, -1ull);
        } else {
            no_match++;
            s390_vec_write_element64(&tmp, i, 0);
        }
        vxc = check_ieee_exc(env, i, false, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    handle_ieee_exc(env, vxc, vec_exc, GETPC());

    *(S390Vector *)v1 = tmp;
    if (set_cc) {
        if (match && no_match) {
            env->cc_op = 1;
        } else if (match) {
            env->cc_op = 0;
        } else {
            env->cc_op = 3;
        }
    }
}

void HELPER(gvec_vfce64)(void *v1, const void *v2, const void *v3,
                         CPUS390XState *env, uint32_t desc)
{
    gvec_vfc64(v1, v2, v3, env, desc, true, false);
}

void HELPER(gvec_vfch64)(void *v1, const void *v2, const void *v3,
                         CPUS390XState *env, uint32_t desc)
{
    gvec_vfc64(v1, v2, v3, env, desc, false, true);
}

void HELPER(gvec_vfche64)(void *v1, const void *v2, const void *v3,
                          CPUS390XState *env, uint32_t desc)
{
    gvec_vfc64(v1, v2, v3, env, desc, true, true);
}

typedef uint64_t (*vfconv64_fn)(uint64_t a, float_status *s);
static void gvec_vfconv64(void *v1, const void *v2, CPUS390XState *env,
                          uint32_t desc, vfconv64_fn fn)
{
    const uint8_t m5 = extract32(simd_data(desc), 4, 4);
    const bool XxC = extract32(simd_data(desc), 2, 1);
    const bool s = extract32(simd_data(desc), 3, 1);
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i, old_mode;

    old_mode = s390_swap_bfp_rounding_mode(env, m5);
    for (i = 0; i < 2; i++) {
        const uint64_t a = s390_vec_read_element64(v2, i);
        const uint64_t ret = fn(a, &env->fpu_status);

        s390_vec_write_element64(&tmp, i, ret);
        vxc = check_ieee_exc(env, i, XxC, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_ieee_exc(env, vxc, vec_exc, GETPC());
    *(S390Vector *)v1 = tmp;
}

static uint64_t gvece_vcgd64(uint64_t a, float_status *s)
{
    return float64_val(int64_to_float64(a, s));
}

void HELPER(gvec_vcgd64)(void *v1, const void *v2, CPUS390XState *env,
                         uint32_t desc)
{
    gvec_vfconv64(v1, v2, env, desc, gvece_vcgd64);
}

static uint64_t gvece_vcdlg64(uint64_t a, float_status *s)
{
    return float64_val(uint64_to_float64(a, s));
}

void HELPER(gvec_vcdlg64)(void *v1, const void *v2, CPUS390XState *env,
                          uint32_t desc)
{
    gvec_vfconv64(v1, v2, env, desc, gvece_vcdlg64);
}

static uint64_t gvece_vcdg64(uint64_t a, float_status *s)
{
    return float64_to_int64(make_float64(a), s);
}

void HELPER(gvec_vcdg64)(void *v1, const void *v2, CPUS390XState *env,
                         uint32_t desc)
{
    gvec_vfconv64(v1, v2, env, desc, gvece_vcdg64);
}

static uint64_t gvece_vclgd64(uint64_t a, float_status *s)
{
    return float64_to_uint64(make_float64(a), s);
}

void HELPER(gvec_vclgd64)(void *v1, const void *v2, CPUS390XState *env,
                          uint32_t desc)
{
    gvec_vfconv64(v1, v2, env, desc, gvece_vclgd64);
}

void HELPER(gvec_vfd64)(void *v1, const void *v2, const void *v3,
                        CPUS390XState *env, uint32_t desc)
{
    gvec_vf2op64(v1, v2, v3, env, desc, float64_div);
}

void HELPER(gvec_vfi64)(void *v1, const void *v2, CPUS390XState *env,
                        uint32_t desc)
{
    const uint8_t m5 = extract32(simd_data(desc), 4, 4);
    const bool XxC = extract32(simd_data(desc), 2, 1);
    const bool s = extract32(simd_data(desc), 3, 1);
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i, old_mode;

    old_mode = s390_swap_bfp_rounding_mode(env, m5);
    for (i = 0; i < 2; i++) {
        const float64 a = make_float64(s390_vec_read_element64(v2, i));
        const uint64_t ret = float64_round_to_int(a, &env->fpu_status);

        s390_vec_write_element64(&tmp, i, (uint64_t)ret);
        vxc = check_ieee_exc(env, i, XxC, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_ieee_exc(env, vxc, vec_exc, GETPC());
    *(S390Vector *)v1 = tmp;
}

void HELPER(gvec_vfll32)(void *v1, const void *v2, CPUS390XState *env,
                         uint32_t desc)
{
    const bool s = extract32(simd_data(desc), 3, 1);
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i;

    for (i = 0; i < 2; i++) {
        /* load from even element */
        const float32 a = make_float32(s390_vec_read_element32(v2, i * 2));
        const uint64_t ret = float64_val(float32_to_float64(a,
                                                            &env->fpu_status));

        s390_vec_write_element64(&tmp, i, ret);
        /* indicate the source element */
        vxc = check_ieee_exc(env, i * 2, 0, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    handle_ieee_exc(env, vxc, vec_exc, GETPC());
    *(S390Vector *)v1 = tmp;
}

void HELPER(gvec_vflr64)(void *v1, const void *v2, CPUS390XState *env,
                         uint32_t desc)
{
    const uint8_t m5 = extract32(simd_data(desc), 4, 4);
    const bool XxC = extract32(simd_data(desc), 2, 1);
    const bool s = extract32(simd_data(desc), 3, 1);
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i, old_mode;

    old_mode = s390_swap_bfp_rounding_mode(env, m5);
    for (i = 0; i < 2; i++) {
        float64 a = make_float64(s390_vec_read_element64(v2, i));
        uint64_t ret = float64_val(float64_to_float32(a, &env->fpu_status));

        /* place at even element */
        s390_vec_write_element32(&tmp, i * 2, ret);
        /* indicate the source element */
        vxc = check_ieee_exc(env, i, XxC, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    s390_restore_bfp_rounding_mode(env, old_mode);
    handle_ieee_exc(env, vxc, vec_exc, GETPC());
    *(S390Vector *)v1 = tmp;
}

void HELPER(gvec_vfm64)(void *v1, const void *v2, const void *v3,
                        CPUS390XState *env, uint32_t desc)
{
    gvec_vf2op64(v1, v2, v3, env, desc, float64_mul);
}

static void gvec_vfma64(void *v1, const void *v2, const void *v3,
                        const void *v4, CPUS390XState *env, uint32_t desc,
                        int flags)
{
    const bool s = extract32(simd_data(desc), 3, 1);
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i;

    for (i = 0; i < 2; i++) {
        float64 a = make_float64(s390_vec_read_element64(v2, i));
        float64 b = make_float64(s390_vec_read_element64(v3, i));
        float64 c = make_float64(s390_vec_read_element64(v4, i));
        uint64_t ret = float64_val(float64_muladd(a, b, c, flags,
                                                  &env->fpu_status));

        s390_vec_write_element64(&tmp, i, ret);
        vxc = check_ieee_exc(env, i, false, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    handle_ieee_exc(env, vxc, vec_exc, GETPC());
    *(S390Vector *)v1 = tmp;
}

void HELPER(gvec_vfma64)(void *v1, const void *v2, const void *v3,
                         const void *v4, CPUS390XState *env, uint32_t desc)
{
    gvec_vfma64(v1, v2, v3, v4, env, desc, 0);
}

void HELPER(gvec_vfms64)(void *v1, const void *v2, const void *v3,
                         const void *v4, CPUS390XState *env, uint32_t desc)
{
    gvec_vfma64(v1, v2, v3, v4, env, desc, float_muladd_negate_c);
}

void HELPER(gvec_vfsq64)(void *v1, const void *v2, CPUS390XState *env,
                         uint32_t desc)
{
    const bool s = extract32(simd_data(desc), 3, 1);
    uint8_t vxc, vec_exc = 0;
    S390Vector tmp = {};
    int i;

    for (i = 0; i < 2; i++) {
        const float64 a = make_float64(s390_vec_read_element64(v2, i));
        const uint64_t ret = float64_val(float64_sqrt(a,&env->fpu_status));

        s390_vec_write_element64(&tmp, i, ret);
        vxc = check_ieee_exc(env, i, false, &vec_exc);
        if (s || vxc) {
            break;
        }
    }
    handle_ieee_exc(env, vxc, vec_exc, GETPC());
    *(S390Vector *)v1 = tmp;
}

void HELPER(gvec_vfs64)(void *v1, const void *v2, const void *v3,
                        CPUS390XState *env, uint32_t desc)
{
    gvec_vf2op64(v1, v2, v3, env, desc, float64_sub);
}

void HELPER(gvec_vftci64)(void *v1, const void *v2, CPUS390XState *env,
                          uint32_t desc)
{
    const uint16_t i3 = extract32(simd_data(desc), 4, 16);
    const bool s = extract32(simd_data(desc), 3, 1);
    int i, match = 0, no_match = 0;

    for (i = 0; i < 2; i++) {
        float64 a = make_float64(s390_vec_read_element64(v2, i));

        if (float64_dcmask(env, a) & i3) {
            match++;
            s390_vec_write_element64(v1, i, -1ull);
        } else {
            no_match++;
            s390_vec_write_element64(v1, i, 0);
        }
        if (s) {
            break;
        }
    }

    if (match && no_match) {
        env->cc_op = 1;
    } else if (match) {
        env->cc_op = 0;
    } else {
        env->cc_op = 3;
    }
}

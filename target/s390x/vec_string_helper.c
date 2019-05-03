/*
 * QEMU TCG support -- s390x vector string instruction support
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
#include "tcg/tcg-gvec-desc.h"
#include "exec/helper-proto.h"

#define DEF_VFAE(BITS)                                                         \
static int vfae##BITS(void *v1, const void *v2, const void *v3, uint8_t m5)    \
{                                                                              \
    const bool in = extract32(m5, 3, 1);                                       \
    const bool rt = extract32(m5, 2, 1);                                       \
    const bool zs = extract32(m5, 1, 1);                                       \
    S390Vector tmp = {};                                                       \
    int first_byte = 16;                                                       \
    int cc = 3; /* no match */                                                 \
    int i, j;                                                                  \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const uint##BITS##_t data = s390_vec_read_element##BITS(v2, i);        \
        bool any_equal = false;                                                \
                                                                               \
        if (zs && !data) {                                                     \
            if (cc == 3) {                                                     \
                first_byte = i * (BITS / 8);                                   \
                cc = 0; /* match for zero */                                   \
            } else if (cc != 0) {                                              \
                cc = 2; /* matching elements before match for zero */          \
            }                                                                  \
            if (!rt) {                                                         \
                break;                                                         \
            }                                                                  \
        }                                                                      \
                                                                               \
        /* try to match with any other element from the other vector */        \
        for (j = 0; j < (128 / BITS); j++) {                                   \
            if (data == s390_vec_read_element##BITS(v3, j)) {                  \
                any_equal = true;                                              \
                break;                                                         \
            }                                                                  \
        }                                                                      \
                                                                               \
        /* invert the result if requested */                                   \
        any_equal = in ^ any_equal;                                            \
        if (cc == 3 && any_equal) {                                            \
            first_byte = i * (BITS / 8);                                       \
            cc = 1; /* matching elements, no match for zero */                 \
            if (!zs && !rt) {                                                  \
                break;                                                         \
            }                                                                  \
        }                                                                      \
        /* indicate bit vector if requested */                                 \
        if (rt && any_equal) {                                                 \
            s390_vec_write_element##BITS(&tmp, i, (uint##BITS##_t)-1ull);      \
        }                                                                      \
    }                                                                          \
    if (!rt) {                                                                 \
        s390_vec_write_element8(&tmp, 7, first_byte);                          \
    }                                                                          \
    *(S390Vector *)v1 = tmp;                                                   \
    return cc;                                                                 \
}
DEF_VFAE(8)
DEF_VFAE(16)
DEF_VFAE(32)

#define DEF_VFAE_HELPER(BITS)                                                  \
void HELPER(gvec_vfae##BITS)(void *v1, const void *v2, const void *v3,         \
                             uint32_t desc)                                    \
{                                                                              \
    vfae##BITS(v1, v2, v3, simd_data(desc));                                   \
}
DEF_VFAE_HELPER(8)
DEF_VFAE_HELPER(16)
DEF_VFAE_HELPER(32)

#define DEF_VFAE_CC_HELPER(BITS)                                               \
void HELPER(gvec_vfae_cc##BITS)(void *v1, const void *v2, const void *v3,      \
                                CPUS390XState *env, uint32_t desc)             \
{                                                                              \
    env->cc_op = vfae##BITS(v1, v2, v3, simd_data(desc));                      \
}
DEF_VFAE_CC_HELPER(8)
DEF_VFAE_CC_HELPER(16)
DEF_VFAE_CC_HELPER(32)

#define DEF_VFEE(BITS)                                                         \
static int vfee##BITS(void *v1, const void *v2, const void *v3, uint8_t m5)    \
{                                                                              \
    const bool zs = extract32(m5, 1, 1);                                       \
    S390Vector tmp = {};                                                       \
    int first_byte = 16;                                                       \
    int cc = 3; /* no match */                                                 \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const uint##BITS##_t data1 = s390_vec_read_element##BITS(v2, i);       \
        const uint##BITS##_t data2 = s390_vec_read_element##BITS(v3, i);       \
                                                                               \
        if (zs && !data1) {                                                    \
            if (cc == 3) {                                                     \
                first_byte = i * (BITS / 8);                                   \
                cc = 0; /* match for zero */                                   \
            } else {                                                           \
                cc = 2; /* matching elements before match for zero */          \
            }                                                                  \
            break;                                                             \
        }                                                                      \
                                                                               \
        if (cc == 3 && data1 == data2) {                                       \
            first_byte = i * (BITS / 8);                                       \
            cc = 1; /* matching elements, no match for zero */                 \
            if (!zs) {                                                         \
                break;                                                         \
            }                                                                  \
        }                                                                      \
    }                                                                          \
    s390_vec_write_element8(&tmp, 7, first_byte);                              \
    *(S390Vector *)v1 = tmp;                                                   \
    return cc;                                                                 \
}
DEF_VFEE(8)
DEF_VFEE(16)
DEF_VFEE(32)

#define DEF_VFEE_HELPER(BITS)                                                  \
void HELPER(gvec_vfee##BITS)(void *v1, const void *v2, const void *v3,         \
                             uint32_t desc)                                    \
{                                                                              \
    vfee##BITS(v1, v2, v3, simd_data(desc));                                   \
}
DEF_VFEE_HELPER(8)
DEF_VFEE_HELPER(16)
DEF_VFEE_HELPER(32)

#define DEF_VFEE_CC_HELPER(BITS)                                               \
void HELPER(gvec_vfee_cc##BITS)(void *v1, const void *v2, const void *v3,      \
                                CPUS390XState *env, uint32_t desc)             \
{                                                                              \
    env->cc_op = vfee##BITS(v1, v2, v3, simd_data(desc));                      \
}
DEF_VFEE_CC_HELPER(8)
DEF_VFEE_CC_HELPER(16)
DEF_VFEE_CC_HELPER(32)

#define DEF_VFENE(BITS)                                                        \
static int vfene##BITS(void *v1, const void *v2, const void *v3, uint8_t m5)   \
{                                                                              \
    const bool zs = extract32(m5, 1, 1);                                       \
    S390Vector tmp = {};                                                       \
    int first_byte = 16;                                                       \
    int cc = 3; /* no match */                                                 \
    int i;                                                                     \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const uint##BITS##_t data1 = s390_vec_read_element##BITS(v2, i);       \
        const uint##BITS##_t data2 = s390_vec_read_element##BITS(v3, i);       \
                                                                               \
        if (data1 != data2) {                                                  \
            first_byte = i * (BITS / 8);                                       \
            cc = data1 < data2 ? 1 : 2; /* inequality found */                 \
            break;                                                             \
        }                                                                      \
                                                                               \
        if (zs && !data1) {                                                    \
            first_byte = i * (BITS / 8);                                       \
            cc = 0; /* match for zero */                                       \
            break;                                                             \
        }                                                                      \
    }                                                                          \
    s390_vec_write_element8(&tmp, 7, first_byte);                              \
    *(S390Vector *)v1 = tmp;                                                   \
    return cc;                                                                 \
}
DEF_VFENE(8)
DEF_VFENE(16)
DEF_VFENE(32)

#define DEF_VFENE_HELPER(BITS)                                                 \
void HELPER(gvec_vfene##BITS)(void *v1, const void *v2, const void *v3,        \
                              uint32_t desc)                                   \
{                                                                              \
    vfene##BITS(v1, v2, v3, simd_data(desc));                                  \
}
DEF_VFENE_HELPER(8)
DEF_VFENE_HELPER(16)
DEF_VFENE_HELPER(32)

#define DEF_VFENE_CC_HELPER(BITS)                                              \
void HELPER(gvec_vfene_cc##BITS)(void *v1, const void *v2, const void *v3,     \
                                CPUS390XState *env, uint32_t desc)             \
{                                                                              \
    env->cc_op = vfene##BITS(v1, v2, v3, simd_data(desc));                     \
}
DEF_VFENE_CC_HELPER(8)
DEF_VFENE_CC_HELPER(16)
DEF_VFENE_CC_HELPER(32)

#define DEF_VISTR(BITS)                                                        \
static int vistr##BITS(void *v1, const void *v2)                               \
{                                                                              \
    S390Vector tmp = {};                                                       \
    int i, cc = 3;                                                             \
                                                                               \
    for (i = 0; i < (128 / BITS); i++) {                                       \
        const uint##BITS##_t data = s390_vec_read_element##BITS(v2, i);        \
                                                                               \
        if (!data) {                                                           \
            cc = 0;                                                            \
            break;                                                             \
        }                                                                      \
        s390_vec_write_element##BITS(&tmp, i, data);                           \
    }                                                                          \
    *(S390Vector *)v1 = tmp;                                                   \
    return cc;                                                                 \
}
DEF_VISTR(8)
DEF_VISTR(16)
DEF_VISTR(32)

#define DEF_VISTR_HELPER(BITS)                                                 \
void HELPER(gvec_vistr##BITS)(void *v1, const void *v2, uint32_t desc)         \
{                                                                              \
    vistr##BITS(v1, v2);                                                       \
}
DEF_VISTR_HELPER(8)
DEF_VISTR_HELPER(16)
DEF_VISTR_HELPER(32)

#define DEF_VISTR_CC_HELPER(BITS)                                              \
void HELPER(gvec_vistr_cc##BITS)(void *v1, const void *v2,                     \
                                 CPUS390XState *env, uint32_t desc)            \
{                                                                              \
    env->cc_op = vistr##BITS(v1, v2);                                          \
}
DEF_VISTR_CC_HELPER(8)
DEF_VISTR_CC_HELPER(16)
DEF_VISTR_CC_HELPER(32)

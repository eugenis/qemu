/*
 *  Vector Integer Support
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


/* Broadcast a value to all elements of a (64-bit) vector.  */
#define V1(X)      (((X) & 0xff) * 0x0101010101010101ull)
#define V2(X)      (((X) & 0xffff) * 0x0001000100010001ull)
#define V4(X)      (((X) & 0xffffffffu) * 0x0000000100000001ull)

static inline int satub(int x)
{
    if (x < 0) {
        return 0;
    } else if (x > 255) {
        return 255;
    } else {
        return x;
    }
}

static inline int satuw(int x)
{
    if (x < 0) {
        return 0;
    } else if (x > 65535) {
        return 65535;
    } else {
        return x;
    }
}

static inline int satsb(int x)
{
    if (x < -128) {
        return -128;
    } else if (x > 127) {
        return 127;
    } else {
        return x;
    }
}

static inline int satsw(int x)
{
    if (x < -32768) {
        return -32768;
    } else if (x > 32767) {
        return 32767;
    } else {
        return x;
    }
}

/* Allow HELPER_BY_PARTS to use 's' or 'u' as the EXTR prefix.  */
#define uextract64  extract64

#define HELPER_BY_PARTS(NAME, SIZE, EXTR, EXPR)		\
uint64_t helper_vec_##NAME(uint64_t va, uint64_t vb)	\
{							\
    uint64_t ret = 0;					\
    int i;						\
    for (i = 0; i < 64; i += SIZE) {			\
        int a = EXTR##extract64(va, i, SIZE);		\
        int b = EXTR##extract64(vb, i, SIZE);		\
        ret = deposit64(ret, i, SIZE, EXPR);		\
    }							\
    return ret;						\
}

uint64_t helper_vec_addb(uint64_t a, uint64_t b)
{
    uint64_t m = V1(0x7f);
    return ((a & m) + (b & m)) ^ ((a ^ b) & ~m);
}

uint64_t helper_vec_addw(uint64_t a, uint64_t b)
{
    uint64_t m = V2(0x7fff);
    return ((a & m) + (b & m)) ^ ((a ^ b) & ~m);
}

uint64_t helper_vec_addd(uint64_t a, uint64_t b)
{
    uint64_t m = 0xffffffffu;
    return ((a & ~m) + (b & ~m)) | ((a + b) & m);
}

HELPER_BY_PARTS(addsb, 8, s, satsb(a + b))
HELPER_BY_PARTS(addsw, 16, s, satsw(a + b))
HELPER_BY_PARTS(addusb, 8, u, satub(a + b))
HELPER_BY_PARTS(addusw, 16, u, satuw(a + b))

HELPER_BY_PARTS(avgb, 8, u, (a + b + 1) >> 1)
HELPER_BY_PARTS(avgw, 16, u, (a + b + 1) >> 1)

HELPER_BY_PARTS(cmpeqb, 8, u, -(a == b))
HELPER_BY_PARTS(cmpeqw, 16, u, -(a == b))
HELPER_BY_PARTS(cmpeqd, 32, u, -(a == b))

HELPER_BY_PARTS(cmpgtb, 8, s, -(a > b))
HELPER_BY_PARTS(cmpgtw, 16, s, -(a > b))
HELPER_BY_PARTS(cmpgtd, 32, s, -(a > b))

HELPER_BY_PARTS(mulhw, 16, s, (a * b) >> 16)
HELPER_BY_PARTS(mulhuw, 16, u, (a * b) >> 16)
HELPER_BY_PARTS(mullw, 16, u, a * b)

uint64_t helper_vec_sllw(uint64_t part, uint64_t shift)
{
    uint64_t mask;

    if (unlikely(shift > 15)) {
        return 0;
    }

    mask = V2(0xffffu >> shift);
    return (part & mask) << shift;
}

uint64_t helper_vec_slld(uint64_t part, uint64_t shift)
{
    uint64_t mask;

    if (unlikely(shift > 31)) {
        return 0;
    }

    mask = V4(0xffffffffu >> shift);
    return (part & mask) << shift;
}

uint64_t helper_vec_sllq(uint64_t part, uint64_t shift)
{
    if (unlikely(shift > 63)) {
        return 0;
    }
    return part << shift;
}

uint64_t helper_vec_sraw(uint64_t part, uint64_t shift)
{
    uint64_t ret = 0;
    int i;

    if (unlikely(shift > 15)) {
        shift = 15;
    }

    for (i = 0; i < 64; i += 16) {
        int32_t t = sextract64(part, i, 16);
        ret = deposit64(ret, i, 16, t >> shift);
    }

    return ret;
}

uint64_t helper_vec_srad(uint64_t part, uint64_t shift)
{
    uint64_t ret = 0;
    int i;

    if (unlikely(shift > 31)) {
        shift = 31;
    }

    for (i = 0; i < 64; i += 32) {
        int32_t t = sextract64(part, i, 32);
        ret = deposit64(ret, i, 32, t >> shift);
    }

    return ret;
}

uint64_t helper_vec_srlw(uint64_t part, uint64_t shift)
{
    uint64_t mask;

    if (unlikely(shift > 15)) {
        return 0;
    }

    mask = V2(0xffffu << shift);
    return (part & mask) >> shift;
}

uint64_t helper_vec_srld(uint64_t part, uint64_t shift)
{
    uint64_t mask;

    if (unlikely(shift > 31)) {
        return 0;
    }

    mask = V2(0xffffffffu << shift);
    return (part & mask) >> shift;
}

uint64_t helper_vec_srlq(uint64_t part, uint64_t shift)
{
    if (unlikely(shift > 63)) {
        return 0;
    }
    return part >> shift;
}

uint64_t helper_vec_subb(uint64_t a, uint64_t b)
{
    uint64_t m = V1(0x80);
    return ((a | m) - (b & ~m)) ^ ((a ^ ~b) & m);
}

uint64_t helper_vec_subw(uint64_t a, uint64_t b)
{
    uint64_t m = V2(0x8000);
    return ((a | m) - (b & ~m)) ^ ((a ^ ~b) & m);
}

uint64_t helper_vec_subd(uint64_t a, uint64_t b)
{
    uint64_t m = 0xffffffffu;
    return ((a & ~m) - (b & ~m)) | ((a - b) & m);
}

HELPER_BY_PARTS(subsb, 8, s, satsb(a - b))
HELPER_BY_PARTS(subsw, 16, s, satsw(a - b))
HELPER_BY_PARTS(subusb, 8, u, satub(a - b))
HELPER_BY_PARTS(subusw, 16, u, satuw(a - b))

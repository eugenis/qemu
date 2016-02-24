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

uint64_t helper_vec_maddwd(uint64_t va, uint64_t vb)
{
    int a0 = sextract64(va, 0, 16);
    int a1 = sextract64(va, 16, 16);
    int a2 = sextract64(va, 32, 16);
    int a3 = sextract64(va, 48, 16);
    int b0 = sextract64(vb, 0, 16);
    int b1 = sextract64(vb, 16, 16);
    int b2 = sextract64(vb, 32, 16);
    int b3 = sextract64(vb, 48, 16);

    a0 *= b0;
    a1 *= b1;
    a2 *= b2;
    a3 *= b3;

    return deposit64(a0 + a1, 32, 32, a2 + a3);
}

HELPER_BY_PARTS(maxub, 8, u, a > b ? a : b)
HELPER_BY_PARTS(minub, 8, u, a < b ? a : b)
HELPER_BY_PARTS(maxsw, 16, s, a > b ? a : b)
HELPER_BY_PARTS(minsw, 16, s, a < b ? a : b)

HELPER_BY_PARTS(mulhw, 16, s, (a * b) >> 16)
HELPER_BY_PARTS(mulhuw, 16, u, (a * b) >> 16)
HELPER_BY_PARTS(mullw, 16, u, a * b)

uint64_t helper_vec_packsswb(uint64_t va, uint64_t vb)
{
    uint64_t ret = 0;
    int i;

    for (i = 0; i < 32; i += 8) {
        int a = sextract64(va, i * 2, 16);
        ret = deposit64(ret, i, 8, satsb(a));
    }
    for (i = 0; i < 32; i += 8) {
        int b = sextract64(vb, i * 2, 16);
        ret = deposit64(ret, i + 32, 8, satsb(b));
    }

    return ret;
}

uint64_t helper_vec_packuswb(uint64_t va, uint64_t vb)
{
    uint64_t ret = 0;
    int i;

    for (i = 0; i < 32; i += 8) {
        int a = extract64(va, i * 2, 16);
        ret = deposit64(ret, i, 8, satub(a));
    }
    for (i = 0; i < 32; i += 8) {
        int b = extract64(vb, i * 2, 16);
        ret = deposit64(ret, i + 32, 8, satub(b));
    }

    return ret;
}

uint64_t helper_vec_packssdw(uint64_t va, uint64_t vb)
{
    uint64_t ret = 0;
    int i;

    for (i = 0; i < 32; i += 16) {
        int a = sextract64(va, i * 2, 32);
        ret = deposit64(ret, i, 16, satsw(a));
    }
    for (i = 0; i < 32; i += 16) {
        int b = sextract64(vb, i * 2, 32);
        ret = deposit64(ret, i + 32, 16, satsw(b));
    }

    return ret;
}

uint64_t helper_vec_sadbw(uint64_t va, uint64_t vb)
{
    int i, ret = 0;

    for (i = 0; i < 64; i += 8) {
        int a = sextract64(va, i, 8);
        int b = sextract64(vb, i, 8);
        int d = a - b;
        ret += d < 0 ? -d : d;
    }

    return ret & 0xffff;
}

uint64_t helper_vec_shufw(uint64_t va, uint32_t sel)
{
    uint16_t r0 = va >> ((sel & 3) * 16);
    uint16_t r1 = va >> (((sel >> 2) & 3) * 16);
    uint16_t r2 = va >> (((sel >> 4) & 3) * 16);
    uint16_t r3 = va >> (((sel >> 6) & 3) * 16);
    uint64_t ret;

    ret = r0;
    ret |= (uint64_t)r1 << 16;
    ret |= (uint64_t)r2 << 32;
    ret |= (uint64_t)r3 << 48;
    return ret;
}

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

uint64_t helper_vec_unpcklbw(uint64_t a, uint64_t b)
{
    unsigned a0 = extract64(a, 0, 8);
    unsigned a1 = extract64(a, 8, 8);
    unsigned a2 = extract64(a, 16, 8);
    unsigned a3 = extract64(a, 24, 8);
    unsigned b0 = extract64(b, 0, 8);
    unsigned b1 = extract64(b, 8, 8);
    unsigned b2 = extract64(b, 16, 8);
    unsigned b3 = extract64(b, 24, 8);
    uint64_t ret;

    ret = a0;
    ret |= (uint64_t)b0 << 8;
    ret |= (uint64_t)a1 << 16;
    ret |= (uint64_t)b1 << 24;
    ret |= (uint64_t)a2 << 32;
    ret |= (uint64_t)b2 << 40;
    ret |= (uint64_t)a3 << 48;
    ret |= (uint64_t)b3 << 56;

    return ret;
}

uint64_t helper_vec_unpckhbw(uint64_t a, uint64_t b)
{
    return helper_vec_unpcklbw(a >> 32, b >> 32);
}

uint64_t helper_vec_unpcklwd(uint64_t a, uint64_t b)
{
    unsigned a0 = extract64(a, 0, 16);
    unsigned a1 = extract64(a, 16, 16);
    unsigned b0 = extract64(b, 0, 16);
    unsigned b1 = extract64(b, 16, 16);
    uint64_t ret;

    ret = a0;
    ret = (uint64_t)b0 << 16;
    ret = (uint64_t)a1 << 32;
    ret = (uint64_t)b1 << 48;

    return ret;
}

uint64_t helper_vec_unpckhwd(uint64_t a, uint64_t b)
{
    return helper_vec_unpcklwd(a >> 32, b >> 32);
}

uint64_t helper_extrq_r(uint64_t in1, uint64_t poslen)
{
    int len = extract32(poslen, 0, 6);
    int pos = extract32(poslen, 8, 6);

    if (len == 0) {
        len = 64;
    }
    if (pos + len > 64) {
        /* This case is undefined, but avoid an assert.  */
        len = 64 - pos;
    }
    return extract64(in1, pos, len);
}

uint64_t helper_insertq_r(uint64_t in1, uint64_t in2, uint64_t poslen)
{
    int len = extract32(poslen, 0, 6);
    int pos = extract32(poslen, 8, 6);

    if (len == 0) {
        len = 64;
    }
    if (pos + len > 64) {
        /* This case is undefined, but avoid an assert.  */
        len = 64 - pos;
    }
    return deposit64(in1, pos, len, in2);
}

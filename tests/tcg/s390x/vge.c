/*
 * QEMU TCG Tests - s390x VECTOR GATHER ELEMENT
 *
 * Copyright (C) 2019 Red Hat Inc
 *
 * Authors:
 *   David Hildenbrand <david@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include <stdint.h>
#include <unistd.h>
#include "signal-helper.inc.c"

static inline void vgef(S390Vector *v1, S390Vector *v2, const void *a2,
                        uint8_t m3)
{
    asm volatile("vgef %[v1], 0(%[v2], %[a2]), %[m3]\n"
                 : [v1] "+v" (v1->v),
                   [v2] "+v" (v2->v)
                 : [a2] "d" (a2),
                   [m3] "i" (m3));
}

static void test_vgef(void)
{
    uint32_t data = 0x12345678ul;
    S390Vector v1 = {
        .d[0] = -1ull,
        .d[1] = -1ull,
    };
    S390Vector v2 = {
        .w[0] = -1,
        .w[1] = -1,
        .w[2] = 56789,
        .w[3] = -1,
    };

    /* load vector element number 2 with the data */
    vgef(&v1, &v2, (uint32_t *)((uint8_t *)&data - 56789), 2);
    check("vgef: element loaded", v1.w[2] == data);
    check("vgef: elements unmodified", v1.w[0] == -1 && v1.w[1] == -1 &&
                                       v1.w[3] == -1);
    check("vgef: source unmodified", v2.d[0] == -1 && v2.w[1] == -1 &&
                                       v2.w[2] == 56789 && v2.w[3] == -1);

    /* invalid element number */
    CHECK_SIGILL(vgef(&v1, &v2, 0, 4));
}

static inline void vgeg(S390Vector *v1, S390Vector *v2, const void *a2,
                        uint8_t m3)
{
    asm volatile("vgeg %[v1], 0(%[v2], %[a2]), %[m3]\n"
                 : [v1] "+v" (v1->v),
                   [v2] "+v" (v2->v)
                 : [a2] "d" (a2),
                   [m3] "i" (m3));
}

static void test_vgeg(void)
{
    uint64_t data = 0x123456789abcdefull;
    S390Vector v1 = {
        .d[0] = -1ull,
        .d[1] = -1ull,
    };
    S390Vector v2 = {
        .d[0] = -1ull,
        .d[1] = 56789,
    };

    /* load vector element number 1 with the data */
    vgeg(&v1, &v2, (uint64_t *)((uint8_t *)&data - 56789), 1);
    check("vgeg: element loaded", v1.d[1] == data);
    check("vgeg: elements unmodified", v1.d[0] == -1ull);
    check("vgeg: source unmodified", v2.d[0] == -1ull && v2.d[1] == 56789);

    /* invalid element number */
    CHECK_SIGILL(vgeg(&v1, &v2, 0, 2));
}

int main(void)
{
    test_vgef();
    test_vgeg();
    return 0;
}

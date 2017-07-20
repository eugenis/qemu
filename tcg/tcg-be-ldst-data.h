/*
 * TCG Backend Data: load-store optimization and data pool.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifdef CONFIG_SOFTMMU

typedef struct TCGLabelQemuLdst {
    bool is_ld;             /* qemu_ld: true, qemu_st: false */
    TCGMemOpIdx oi;
    TCGType type;           /* result type of a load */
    TCGReg addrlo_reg;      /* reg index for low word of guest virtual addr */
    TCGReg addrhi_reg;      /* reg index for high word of guest virtual addr */
    TCGReg datalo_reg;      /* reg index for low word to be loaded or stored */
    TCGReg datahi_reg;      /* reg index for high word to be loaded or stored */
    tcg_insn_unit *raddr;   /* gen code addr of the next IR of qemu_ld/st IR */
    tcg_insn_unit *label_ptr[2]; /* label pointers to be updated */
    struct TCGLabelQemuLdst *next;
} TCGLabelQemuLdst;

#endif

typedef struct TCGLabelPoolData {
    struct TCGLabelPoolData *next;
    tcg_target_ulong data;
    tcg_insn_unit *label;
    int type, addend;
} TCGLabelPoolData;

typedef struct TCGBackendData {
#ifdef CONFIG_SOFTMMU
    TCGLabelQemuLdst *ldst;
#endif
    TCGLabelPoolData *pool;
} TCGBackendData;


/*
 * Initialize TB backend data at the beginning of the TB.
 */

static inline void tcg_out_tb_init(TCGContext *s)
{
    memset(s->be, 0, sizeof(TCGBackendData));
}

#ifdef CONFIG_SOFTMMU
/*
 * Generate TB finalization at the end of block
 */

static void tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *l);
static void tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *l);

/*
 * Allocate a new TCGLabelQemuLdst entry.
 */

static inline TCGLabelQemuLdst *new_ldst_label(TCGContext *s)
{
    TCGBackendData *be = s->be;
    TCGLabelQemuLdst *l = tcg_malloc(sizeof(*l));

    l->next = be->ldst;
    be->ldst = l;
    return l;
}

static bool finalize_ldst(TCGContext *s)
{
    TCGLabelQemuLdst *lb;

    /* qemu_ld/st slow paths */
    for (lb = s->be->ldst; lb != NULL; lb = lb->next) {
        if (lb->is_ld) {
            tcg_out_qemu_ld_slow_path(s, lb);
        } else {
            tcg_out_qemu_st_slow_path(s, lb);
        }

        /* Test for (pending) buffer overflow.  The assumption is that any
           one operation beginning below the high water mark cannot overrun
           the buffer completely.  Thus we can test for overflow after
           generating code without having to check during generation.  */
        if (unlikely((void *)s->code_ptr > s->code_gen_highwater)) {
            return false;
        }
    }
    return true;
}

#else
static inline bool finalize_ldst(TCGContext *s) { return true; }
#endif /* CONFIG_SOFTMMU */

static void new_pool_label(TCGContext *s, tcg_target_ulong data, int type,
                           tcg_insn_unit *label, int addend)
{
    TCGLabelPoolData *n = tcg_malloc(sizeof(*n));
    TCGLabelPoolData *i, **pp;

    n->data = data;
    n->label = label;
    n->type = type;
    n->addend = addend;

    /* Insertion sort on the pool.  */
    for (pp = &s->be->pool; (i = *pp) && i->data < data; pp = &i->next) {
        continue;
    }
    n->next = *pp;
    *pp = n;
}

static void tcg_out_nop_fill(tcg_insn_unit *p, int count);

static bool finalize_pool(TCGContext *s)
{
    TCGLabelPoolData *p = s->be->pool;
    tcg_target_ulong d, *a;

    if (p == NULL) {
        return true;
    }

    // a = (void *)ROUND_UP((uintptr_t)s->code_ptr, qemu_icache_linesize);
    a = (void *)ROUND_UP((uintptr_t)s->code_ptr, sizeof(tcg_target_ulong));
    tcg_out_nop_fill(s->code_ptr, (tcg_insn_unit *)a - s->code_ptr);
    s->data_gen_ptr = a;

    /* Ensure the first comparison fails.  */
    d = p->data + 1;

    for (; p != NULL; p = p->next) {
        if (p->data != d) {
            d = p->data;
            if (unlikely((void *)a > s->code_gen_highwater)) {
                return false;
            }
            *a++ = d;
        }
        patch_reloc(p->label, p->type, (intptr_t)(a - 1), p->addend);

    }

    s->code_ptr = (void *)a;
    return true;
}

static bool tcg_out_tb_finalize(TCGContext *s)
{
    return finalize_ldst(s) && finalize_pool(s);
}

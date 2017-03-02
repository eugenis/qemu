#ifndef QEMU_TCI_H
#define QEMU_TCI_H

typedef enum {
    TCI_invalid,

    /* Binary opcodes.  */

    TCI_add,
    TCI_sub,
    TCI_mul,
    TCI_divu,
    TCI_remu,
    TCI_divs,
    TCI_rems,
    TCI_and,
    TCI_ior,
    TCI_xor,
    TCI_andc,
    TCI_iorc,
    TCI_xorc,
    TCI_nand,
    TCI_nior,
    TCI_shl,
    TCI_shr4,
    TCI_sar4,
    TCI_rol4,
    TCI_ror4,
    TCI_shr8,
    TCI_sar8,
    TCI_rol8,
    TCI_ror8,

    TCI_cmp4eq,
    TCI_cmp4ne,
    TCI_cmp4lt,
    TCI_cmp4le,
    TCI_cmp4gt,
    TCI_cmp4ge,
    TCI_cmp4ltu,
    TCI_cmp4leu,
    TCI_cmp4gtu,
    TCI_cmp4geu,

    TCI_cmp8eq,
    TCI_cmp8ne,
    TCI_cmp8lt,
    TCI_cmp8le,
    TCI_cmp8gt,
    TCI_cmp8ge,
    TCI_cmp8ltu,
    TCI_cmp8leu,
    TCI_cmp8gtu,
    TCI_cmp8geu,

    TCI_extract,
    TCI_sextract,

    TCI_ctz,
    TCI_clz,

    TCI_movc,
    TCI_concat4,
    TCI_LAST_BINARY_OPC = TCI_concat4,

    /* Unary opcodes.  */

    TCI_ctpop,
    TCI_bswap2,
    TCI_bswap4,
    TCI_bswap8,
    TCI_LAST_UNARY_OPC = TCI_bswap8,

    /* Control flow opcodes.  */

    TCI_b,
    TCI_bc,
    TCI_exit,
    TCI_call0,
    TCI_call4,
    TCI_call8,
    TCI_goto_tb,
    TCI_goto_ptr,

    /* Qemu load/store operations.  */

    TCI_qst1,
    TCI_qst2_le,
    TCI_qst2_be,
    TCI_qst4_le,
    TCI_qst4_be,
    TCI_qst8_le,
    TCI_qst8_be,

    TCI_qld1u,
    TCI_qld1s,
    TCI_qld2u_le,
    TCI_qld2u_be,
    TCI_qld2s_le,
    TCI_qld2s_be,
    TCI_qld4u_le,
    TCI_qld4u_be,
    TCI_qld4s_le,
    TCI_qld4s_be,
    TCI_qld8_le,
    TCI_qld8_be,

    /* Load and store opcodes.  */

    TCI_st1,
    TCI_st2,
    TCI_st4,
    TCI_st8,

    TCI_ld1u,
    TCI_ld1s,
    TCI_ld2u,
    TCI_ld2s,
    TCI_ld4u,
    TCI_ld4s,
    TCI_ld8,

    /* Zero-ary opcodes.  */

    TCI_setc,
    TCI_mb,

    /* 3 and 4-operand opcodes.  */

    TCI_cmppeq,
    TCI_cmppne,
    TCI_cmpplt,
    TCI_cmpple,
    TCI_cmppgt,
    TCI_cmppge,
    TCI_cmppltu,
    TCI_cmppleu,
    TCI_cmppgtu,
    TCI_cmppgeu,

    TCI_add2,
    TCI_sub2,
    TCI_mulu2,
    TCI_muls2,

    TCI_deposit,

    TCI_NUM_OPC
} TCIOp;

#define LEN_Y   14
#define LEN_X   5
#define LEN_W   3
#define LEN_R   3
#define LEN_OP  7

#define POS_Y   0
#define POS_X   LEN_Y
#define POS_W   (POS_X + LEN_X)
#define POS_R   (POS_W + LEN_W)
#define POS_OP  (POS_R + LEN_R)

#define BIAS_Y  (1 << (LEN_Y - 1))
#define BIAS_X  (1 << (LEN_X - 1))

#define MAX_Y   ((1 << LEN_Y) - 1 - BIAS_Y)
#define MAX_X   ((1 << LEN_X) - 1 - BIAS_X)

#define MIN_Y   (9 - BIAS_Y)
#define MIN_X   (9 - BIAS_X)

QEMU_BUILD_BUG_ON(POS_OP + LEN_OP != 32);
QEMU_BUILD_BUG_ON(TCI_NUM_OPC > (1 << LEN_OP));

#endif /* QEMU_TCI_H */

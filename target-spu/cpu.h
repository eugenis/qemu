/*
 *  Synergistic Processor Unit (SPU) emulation
 *  CPU definitions for qemu.
 *
 *  Copyright (c) 2011  Richard Henderson
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

#ifndef QEMU_SPU_CPU_H
#define QEMU_SPU_CPU_H 1

#include "qemu-common.h"

#define TARGET_LONG_BITS 32

#define CPUArchState struct CPUSPUState

#include "exec/cpu-defs.h"
#include "fpu/softfloat.h"

/* The SPU does not have virtual memory, but match the PowerPC host.  */
#define TARGET_PAGE_BITS 12

#define TARGET_PHYS_ADDR_SPACE_BITS  32
#define TARGET_VIRT_ADDR_SPACE_BITS  32

#ifndef EM_SPU
#define EM_SPU  23  /* Sony/Toshiba/IBM SPU */
#endif
#define ELF_MACHINE  EM_SPU

/* The one "mode" is physical addressing.  */
#define NB_MMU_MODES 1

typedef struct CPUSPUState {
    uint32_t gpr[128*4];
    uint32_t pc;
    uint32_t srr0;
    uint32_t lslr;

    /* Those resources are used only in QEMU core.  */
    CPU_COMMON
    int error_code;
} CPUSPUState;

#include "exec/cpu-all.h"
#include "cpu-qom.h"

enum {
    EXCP_RESET,
    EXCP_ILLOPC,
    EXCP_MMFAULT
};

#ifndef CONFIG_USER_ONLY
extern const struct VMStateDescription vmstate_spu_cpu;
#endif

void spu_translate_init(void);
SPUCPU *cpu_spu_init(const char *cpu_model);

static inline CPUSPUState *cpu_init(const char *cpu_model)
{
    SPUCPU *cpu = cpu_spu_init(cpu_model);
    if (cpu == NULL) {
        return NULL;
    }
    return &cpu->env;
}

void spu_cpu_list(FILE *f, fprintf_function cpu_fprintf);
int spu_cpu_exec(CPUState *s);
int spu_cpu_signal_handler(int host_signum, void *pinfo, void *puc);
int spu_cpu_handle_mmu_fault(CPUState *, vaddr, int, int);
void spu_cpu_do_interrupt(CPUState *);
hwaddr spu_cpu_get_phys_page_debug(CPUState *, vaddr);
void spu_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                        int flags);

#define cpu_list spu_cpu_list
#define cpu_exec spu_cpu_exec
#define cpu_signal_handler cpu_spu_signal_handler

static inline void cpu_get_tb_cpu_state(CPUSPUState *env, target_ulong *pc,
                                        target_ulong *cs_base,
                                        uint32_t *pflags)
{
    *pc = env->pc;
    *cs_base = 0;
    *pflags = 0;
}

static inline int cpu_mmu_index(CPUSPUState *env, bool ifetch)
{
    return 0;
}

#include "exec/exec-all.h"

static inline void cpu_pc_from_tb(CPUSPUState *env, TranslationBlock *tb)
{
    env->pc = tb->pc;
}

#endif

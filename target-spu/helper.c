/*
 *  Synergistic Processor Unit (SPU) emulation
 *  Non-opcode helper functions.
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

#include "qemu/osdep.h"
#include "cpu.h"

int spu_cpu_handle_mmu_fault(CPUState *cs, vaddr address, int rw, int mmu_idx)
{
    cs->exception_index = EXCP_MMFAULT;
    return 1;
}

hwaddr spu_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    return addr;
}

void spu_cpu_do_interrupt(CPUState *cs)
{
    SPUCPU *cpu = SPU_CPU(cs);
    CPUSPUState *env = &cpu->env;
    
    cs->exception_index = -1;
    env->srr0 = env->pc;
    env->pc = 0;
}

void spu_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                        int flags)
{
    SPUCPU *cpu = SPU_CPU(cs);
    CPUSPUState *env = &cpu->env;
    int i;

    cpu_fprintf(f, "  PC %08x\n", env->pc);
    for (i = 0; i < 128; ++i) {
        cpu_fprintf(f, "$%03d %08x %08x %08x %08x%s",
                    i, env->gpr[i*4 + 0], env->gpr[i*4 + 1],
                    env->gpr[i*4 + 2], env->gpr[i*4 + 3],
                    i & 1 ? "  " : "\n");
    }
}

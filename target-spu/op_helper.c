/*
 *  Synergistic Processor Unit (SPU) emulation
 *  Opcode helper functions.
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
#include "qemu/host-utils.h"
#include "fpu/softfloat.h"
#include "exec/helper-proto.h"


void QEMU_NORETURN helper_debug(CPUSPUState *env)
{
    SPUCPU *cpu = spu_env_get_cpu(env);
    CPUState *cs = CPU(cpu);

    cs->exception_index = EXCP_DEBUG;
    env->error_code = 0;
    cpu_loop_exit(cs);
}

void tlb_fill(CPUState *cs, target_ulong addr, int is_write,
              int mmu_idx, uintptr_t retaddr)
{
    abort();
}

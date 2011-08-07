/*
 * QEMU SPU CPU
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */
#ifndef QEMU_SPU_CPU_QOM_H
#define QEMU_SPU_CPU_QOM_H

#include "qom/cpu.h"
#include "cpu.h"

#define TYPE_SPU_CPU "spu-cpu"

#define SPU_CPU_CLASS(klass) \
    OBJECT_CLASS_CHECK(SPUCPUClass, (klass), TYPE_SPU_CPU)
#define SPU_CPU(obj) \
    OBJECT_CHECK(SPUCPU, (obj), TYPE_SPU_CPU)
#define SPU_CPU_GET_CLASS(obj) \
    OBJECT_GET_CLASS(SPUCPUClass, (obj), TYPE_SPU_CPU)

/**
 * SPUCPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_reset: The parent class' reset handler.
 *
 * An SPU CPU model.
 */
typedef struct SPUCPUClass {
    /*< private >*/
    CPUClass parent_class;
    /*< public >*/

    DeviceRealize parent_realize;
    void (*parent_reset)(CPUState *cpu);
} SPUCPUClass;

/**
 * SPUCPU:
 * @env: #CPUSPUState
 *
 * An SPU CPU.
 */
typedef struct SPUCPU {
    /*< private >*/
    CPUState parent_obj;
    /*< public >*/

    CPUSPUState env;
} SPUCPU;

static inline SPUCPU *spu_env_get_cpu(CPUSPUState *env)
{
    return SPU_CPU(container_of(env, SPUCPU, env));
}

#define ENV_GET_CPU(e) CPU(spu_env_get_cpu(e))

#define ENV_OFFSET offsetof(SPUCPU, env)

#endif

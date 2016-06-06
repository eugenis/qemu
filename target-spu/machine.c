/*
 *  Synergistic Processor Unit (SPU) emulation
 *  Machine save and restore.
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
#include "qemu-common.h"
#include "cpu.h"
#include "hw/hw.h"
#include "hw/boards.h"
#include "migration/cpu.h"
#include "exec/helper-proto.h"


static VMStateField vmstate_env_fields[] = {
    VMSTATE_UINTTL_ARRAY(gpr, CPUSPUState, 128*4),
    VMSTATE_UINTTL(pc, CPUSPUState),
    VMSTATE_UINTTL(srr0, CPUSPUState),
    VMSTATE_UINTTL(inte, CPUSPUState),
    VMSTATE_UINTTL(lslr, CPUSPUState),
    VMSTATE_UINTTL_ARRAY(ret, CPUSPUState, 4), /* fscr */
    VMSTATE_END_OF_LIST()
};

static void env_pre_save(void *opaque)
{
    CPUSPUState *env = opaque;
    helper_fscrrd(env);
}

static int env_post_load(void *opaque, int version_id)
{
    CPUSPUState *env = opaque;
    helper_fscrwr(env, env->ret[0], env->ret[1], env->ret[2], env->ret[3]);
    return 0;
}

static const VMStateDescription vmstate_env = {
    .name = "env",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = env_pre_save,
    .post_load = env_post_load,
    .fields = vmstate_env_fields,
};

static VMStateField vmstate_cpu_fields[] = {
    VMSTATE_CPU(),
    VMSTATE_STRUCT(env, SPUCPU, 1, vmstate_env, CPUSPUState),
    VMSTATE_END_OF_LIST()
};

const VMStateDescription vmstate_spu_cpu = {
    .name = "cpu",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields = vmstate_cpu_fields,
};

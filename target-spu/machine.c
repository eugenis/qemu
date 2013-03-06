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

static VMStateField vmstate_env_fields[] = {
    VMSTATE_UINTTL_ARRAY(gpr, CPUSPUState, 128*4),
    VMSTATE_UINTTL(pc, CPUSPUState),
    VMSTATE_UINTTL(srr0, CPUSPUState),
    VMSTATE_UINTTL(lslr, CPUSPUState),
    VMSTATE_END_OF_LIST()
};

static const VMStateDescription vmstate_env = {
    .name = "env",
    .version_id = 1,
    .minimum_version_id = 1,
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

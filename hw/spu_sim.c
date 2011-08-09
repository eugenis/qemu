/*
 *  Synergistic Processor Unit (SPU) emulation
 *  Simple Simulator.
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
#include "hw/loader.h"
#include "elf.h"
#include "exec/address-spaces.h"


static void spu_sim_init(MachineState *machine)
{
    const char *kernel_filename;
    ram_addr_t ram_size;
    MemoryRegion *ram;
    int kernel_size;
    uint64_t entry;
    SPUCPU *cpu;

    cpu = cpu_spu_init(machine->cpu_model);
    if (cpu == NULL) {
        hw_error("unable to find cpu model\n");
        exit(0);
    }

    ram_size = machine->ram_size;
    if (ram_size & (ram_size - 1)) {
        fprintf(stderr, "qemu: RAM size not a power of 2\n");
        exit(1);
    }
    cpu->env.lslr = ram_size - 1;

    /* Allocate RAM */
    ram = g_malloc(sizeof(*ram));
    memory_region_allocate_system_memory(ram, NULL, "spu.ram", ram_size);
    memory_region_add_subregion(get_system_memory(), 0, ram);

    /* Load the program to run.  */
    kernel_filename = machine->kernel_filename;
    if (kernel_filename == NULL) {
        hw_error("no executable specified\n");
        exit(1);
    }
    kernel_size = load_elf(kernel_filename, NULL, NULL,
                           &entry, NULL, NULL, 1, ELF_MACHINE, 0, 0);
    if (kernel_size < 0) {
        hw_error("could not load executable '%s'\n", kernel_filename);
        exit(1);
    }
    cpu->env.pc = entry;
}

static void spu_sim_machine_init(MachineClass *mc)
{
    mc->desc = "Simulator";
    mc->init = spu_sim_init;
    mc->max_cpus = 1;
    mc->is_default = 1;
}
DEFINE_MACHINE("sim", spu_sim_machine_init);

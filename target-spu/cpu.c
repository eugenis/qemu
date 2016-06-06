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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "qemu-common.h"

static void spu_cpu_set_pc(CPUState *cs, vaddr value)
{
    SPUCPU *cpu = SPU_CPU(cs);

    cpu->env.pc = value;
}

static void spu_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    SPUCPUClass *scc = SPU_CPU_GET_CLASS(dev);

    qemu_init_vcpu(cs);

    scc->parent_realize(dev, errp);
}

/* Sort alphabetically by type name. */
static gint spu_cpu_list_compare(gconstpointer a, gconstpointer b)
{
    ObjectClass *class_a = (ObjectClass *)a;
    ObjectClass *class_b = (ObjectClass *)b;
    const char *name_a, *name_b;

    name_a = object_class_get_name(class_a);
    name_b = object_class_get_name(class_b);
    return strcmp(name_a, name_b);
}

static void spu_cpu_list_entry(gpointer data, gpointer user_data)
{
    ObjectClass *oc = data;
    CPUListState *s = user_data;

    (*s->cpu_fprintf)(s->file, "  %s\n",
                      object_class_get_name(oc));
}

void spu_cpu_list(FILE *f, fprintf_function cpu_fprintf)
{
    CPUListState s = {
        .file = f,
        .cpu_fprintf = cpu_fprintf,
    };
    GSList *list;

    list = object_class_get_list(TYPE_SPU_CPU, false);
    list = g_slist_sort(list, spu_cpu_list_compare);
    (*cpu_fprintf)(f, "Available CPUs:\n");
    g_slist_foreach(list, spu_cpu_list_entry, &s);
    g_slist_free(list);
}

/* Models */

static ObjectClass *spu_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc = NULL;

    if (cpu_model == NULL) {
        return NULL;
    }

    oc = object_class_by_name(cpu_model);
    if (oc != NULL
        && object_class_dynamic_cast(oc, TYPE_SPU_CPU) != NULL
        && !object_class_is_abstract(oc)) {
        return oc;
    }
    return oc;
}

SPUCPU *cpu_spu_init(const char *cpu_model)
{
    SPUCPU *cpu;
    ObjectClass *cpu_class;

    cpu_class = spu_cpu_class_by_name(cpu_model);
    if (cpu_class == NULL) {
        return NULL;
    }
    cpu = SPU_CPU(object_new(object_class_get_name(cpu_class)));

    object_property_set_bool(OBJECT(cpu), true, "realized", NULL);

    return cpu;
}

static void spu_cpu_initfn(Object *obj)
{
    CPUState *cs = CPU(obj);
    SPUCPU *cpu = SPU_CPU(obj);

    cs->env_ptr = &cpu->env;
    cpu_exec_init(cs, &error_abort);

    spu_translate_init();
}

static void spu_cpu_reset(CPUState *s)
{
    SPUCPU *cpu = SPU_CPU(s);
    SPUCPUClass *scc = SPU_CPU_GET_CLASS(cpu);
    CPUSPUState *env = &cpu->env;
    int i;

    scc->parent_reset(s);

    memset(env, 0, offsetof(CPUSPUState, lslr));

    for (i = 0; i < 4; ++i) {
        float_status *st = &env->sp_status[i];
        set_float_rounding_mode(float_round_to_zero, st);
        set_flush_to_zero(true, st);
        set_flush_inputs_to_zero(true, st);
    }
}

static void spu_cpu_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    SPUCPUClass *scc = SPU_CPU_CLASS(oc);

    scc->parent_realize = dc->realize;
    dc->realize = spu_cpu_realizefn;

    scc->parent_reset = cc->reset;
    cc->reset = spu_cpu_reset;

    cc->class_by_name = spu_cpu_class_by_name;
    cc->do_interrupt = spu_cpu_do_interrupt;
    cc->dump_state = spu_cpu_dump_state;
    cc->set_pc = spu_cpu_set_pc;
    cc->handle_mmu_fault = spu_cpu_handle_mmu_fault;
    cc->get_phys_page_debug = spu_cpu_get_phys_page_debug;
    dc->vmsd = &vmstate_spu_cpu;
}

static const TypeInfo spu_cpu_type_info = {
    .name = TYPE_SPU_CPU,
    .parent = TYPE_CPU,
    .instance_size = sizeof(SPUCPU),
    .instance_init = spu_cpu_initfn,
    .abstract = false,
    .class_size = sizeof(SPUCPUClass),
    .class_init = spu_cpu_class_init,
};

static void spu_cpu_register_types(void)
{
    type_register_static(&spu_cpu_type_info);
}

type_init(spu_cpu_register_types)

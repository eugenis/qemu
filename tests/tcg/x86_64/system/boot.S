/*
 * x86_64 boot and support code
 *
 * Copyright 2019 Linaro
 *
 * This work is licensed under the terms of the GNU GPL, version 3 or later.
 * See the COPYING file in the top-level directory.
 *
 * Unlike the i386 version we instead use Xen's PVHVM booting header
 * which should drop us automatically in 64 bit mode ready to go. I've
 * nabbed bits of the Linux kernel setup to achieve this.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

        .section .head

#define ELFNOTE_START(name, type, flags)	\
.pushsection .note.name, flags,@note	;	\
  .balign 4				;	\
  .long 2f - 1f		/* namesz */	;	\
  .long 4484f - 3f	/* descsz */	;	\
  .long type				;	\
1:.asciz #name				;	\
2:.balign 4				;	\
3:

#define ELFNOTE_END				\
4484:.balign 4				;	\
.popsection				;

#define ELFNOTE(name, type, desc)		\
	ELFNOTE_START(name, type, "")		\
		desc			;	\
	ELFNOTE_END

#define XEN_ELFNOTE_ENTRY          1
#define XEN_ELFNOTE_HYPERCALL_PAGE 2
#define XEN_ELFNOTE_VIRT_BASE      3
#define XEN_ELFNOTE_PADDR_OFFSET   4
#define XEN_ELFNOTE_PHYS32_ENTRY  18

#define __ASM_FORM(x)	x
#define __ASM_FORM_RAW(x)     x
#define __ASM_FORM_COMMA(x) x,
#define __ASM_SEL(a,b)           __ASM_FORM(b)
#define __ASM_SEL_RAW(a,b)      __ASM_FORM_RAW(b)
#define _ASM_PTR	__ASM_SEL(.long, .quad)

	ELFNOTE(Xen, XEN_ELFNOTE_VIRT_BASE,      _ASM_PTR 0x100000)
	ELFNOTE(Xen, XEN_ELFNOTE_ENTRY,          _ASM_PTR _start)
	ELFNOTE(Xen, XEN_ELFNOTE_PHYS32_ENTRY,   _ASM_PTR 0)    /* entry == virtbase */
	ELFNOTE(Xen, XEN_ELFNOTE_PADDR_OFFSET,   _ASM_PTR 0)

        .code64
        .section .text
        /* Kernel Entry Point */
.global _start
_start:
	// Setup stack ASAP
	movq $stack_end,%rsp


        /* don't worry about stack frame, assume everthing is garbage when we return */
	call main

        /* output any non-zero result in eax to isa-debug-exit device */
        test %al, %al
        jz 1f
        out %ax, $0xf4

1:      /* QEMU ACPI poweroff */
	mov $0x604,%edx
	mov $0x2000,%eax
	out %ax,%dx
	hlt
	jmp 1b

        /*
         * Helper Functions
         *
         * x86_64 calling convention is rdi, rsi, rdx, rcx, r8, r9
         */

        /* Output a single character to serial port */
        .global __sys_outc
__sys_outc:
        pushq %rax
        mov %rax, %rdx
	out %al,$0xE9
        popq %rax
        ret

        .section .bss
        .align 16

stack: .space 65536
stack_end:

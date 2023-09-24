/*	$NetBSD: cpufunc.c,v 1.49 2020/07/19 07:35:08 maxv Exp $	*/

/*-
 * Copyright (c) 1998, 2007, 2020 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum, and by Andrew Doran.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Functions to provide access to i386-specific instructions.
 *
 * These are shared with NetBSD/xen.
 */

// based on the source of sys/arch/i386/i386/cpufunc.S

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: cpufunc.S,v 1.49 2020/07/19 07:35:08 maxv Exp $");

#include <sys/types.h>
#include <sys/errno.h>

#include "opt_xen.h"

#include <machine/specialreg.h>
#include <machine/segments.h>

#include <wasm/wasm_module.h>

void __panic_abort(void) __WASM_IMPORT(kern, panic_abort);

void
x86_lfence(void)
{
#if 0 // TODO: wasm fixme
	lock
	addl	$0, -4(%esp)
	ret
#endif
}

void
x86_sfence(void)
{
#if 0 // TODO: wasm fixme
	lock
	addl	$0, -4(%esp)
	ret
#endif
}

void
x86_mfence(void)
{
#if 0 // TODO: wasm fixme
	lock
	addl	$0, -4(%esp)
	ret
#endif
}

#ifndef XENPV
// also is related to virtual memory gdt
void
lidt(struct region_descriptor *arg)
{
#if 0 // TODO: wasm fixme
	movl	4(%esp), %eax
	lidt	(%eax)
	ret
#endif
    //__panic_abort();
}

void
x86_hotpatch(uint8_t arg1, uint8_t arg2)
{
#if 0 // TODO: wasm fixme
	/* save EFLAGS, and disable intrs */
	pushfl
	cli

	/* save CR0, and disable WP */
	movl	%cr0,%ecx
	pushl	%ecx
	andl	$~CR0_WP,%ecx
	movl	%ecx,%cr0

	pushl	4*4(%esp) /* arg2 */
	pushl	4*4(%esp) /* arg1 */
	call	_C_LABEL(x86_hotpatch_apply)
	addl	$2*4,%esp

	/* write back and invalidate cache */
	wbinvd

	/* restore CR0 */
	popl	%ecx
	movl	%ecx,%cr0

	/* flush instruction pipeline */
	pushl	%eax
	call	x86_flush
	popl	%eax

	/* clean up */
	pushl	%eax
	call	_C_LABEL(x86_hotpatch_cleanup)
	addl	$4,%esp

	/* restore RFLAGS */
	popfl
	ret
#endif
    __panic_abort();
}
#endif /* XENPV */

u_long
x86_read_flags(void)
{
#if 0 // TODO: wasm fixme
	pushfl
	popl	%eax
	ret
#endif
    __panic_abort();
    return 0;
}

void
x86_write_flags(u_long flags)
{
#if 0 // TODO: wasm fixme
	movl	4(%esp), %eax
	pushl	%eax
	popfl
	ret
#endif
    __panic_abort();
}

#ifndef XENPV
u_long x86_write_psl(void) __attribute__((alias("x86_write_flags")));
void x86_read_psl(u_long) __attribute__((alias("x86_read_flags")));
#endif	/* XENPV */

/*
 * Support for reading MSRs in the safe manner (returns EFAULT on fault)
 */
/* int rdmsr_safe(u_int msr, uint64_t *data) */
int
rdmsr_safe(u_int msr, uint64_t *data)
{
#if 0 // TODO: wasm fixme
	movl	CPUVAR(CURLWP), %ecx
	movl	L_PCB(%ecx), %ecx
	movl	$_C_LABEL(msr_onfault), PCB_ONFAULT(%ecx)

	movl	4(%esp), %ecx /* u_int msr */
	rdmsr
	movl	8(%esp), %ecx /* *data */
	movl	%eax, (%ecx)  /* low-order bits */
	movl	%edx, 4(%ecx) /* high-order bits */
	xorl	%eax, %eax    /* "no error" */

	movl	CPUVAR(CURLWP), %ecx
	movl	L_PCB(%ecx), %ecx
	movl	%eax, PCB_ONFAULT(%ecx)

	ret
#endif
    __panic_abort();
    return 0;
}

#if 0
/*
 * MSR operations fault handler
 */
_type_
msr_onfault(_type_)
{
#if 0 // TODO: wasm fixme
	movl	CPUVAR(CURLWP), %ecx
	movl	L_PCB(%ecx), %ecx
	movl	$0, PCB_ONFAULT(%ecx)
	movl	$EFAULT, %eax
	ret
#endif
    __panic_abort();
}
#endif

#if 0

#define ADD_counter32	addl	CPUVAR(CC_SKEW), %eax
#define ADD_counter	ADD_counter32			;\
			adcl	CPUVAR(CC_SKEW+4), %edx

#define SERIALIZE_lfence	lfence
#define SERIALIZE_mfence	mfence

#define CPU_COUNTER_FENCE(counter, fence) \
_type_
cpu_ ## counter ## _ ## fence(_type_)
{	;\
	pushl	%ebx			;\
	movl	CPUVAR(CURLWP), %ecx	;\
1:					;\
	movl	L_NCSW(%ecx), %ebx	;\
	SERIALIZE_ ## fence		;\
	rdtsc				;\
	ADD_ ## counter			;\
	cmpl	%ebx, L_NCSW(%ecx)	;\
	jne	2f			;\
	popl	%ebx			;\
	ret				;\
2:					;\
	jmp	1b			;\
}

CPU_COUNTER_FENCE(counter, lfence)
CPU_COUNTER_FENCE(counter, mfence)
CPU_COUNTER_FENCE(counter32, lfence)
CPU_COUNTER_FENCE(counter32, mfence)

#define CPU_COUNTER_CPUID(counter)	\
_type_
cpu_ ## counter ## _cpuid(_type_)
{	;\
	pushl	%ebx			;\
	pushl	%esi			;\
	movl	CPUVAR(CURLWP), %ecx	;\
1:					;\
	movl	L_NCSW(%ecx), %esi	;\
	pushl	%ecx			;\
	xor	%eax, %eax		;\
	cpuid				;\
	rdtsc				;\
	ADD_ ## counter			;\
	popl	%ecx			;\
	cmpl	%esi, L_NCSW(%ecx)	;\
	jne	2f			;\
	popl	%esi			;\
	popl	%ebx			;\
	ret				;\
2:					;\
	jmp	1b			;\
}

CPU_COUNTER_CPUID(counter)
CPU_COUNTER_CPUID(counter32)

#endif

void
breakpoint(void)
{
#if 0 // TODO: wasm fixme
	pushl	%ebp
	movl	%esp, %ebp
	int	$0x03		/* paranoid, not 'int3' */
	popl	%ebp
	ret
#endif
    __panic_abort();
}

#if 0
_type_
x86_curcpu(_type_)
{
	movl	%fs:(CPU_INFO_SELF), %eax
	ret
}

_type_
x86_curlwp(_type_)
{
	movl	%fs:(CPU_INFO_CURLWP), %eax
	ret
}

_type_
__byte_swap_u32_variable(_type_)
{
	movl	4(%esp), %eax
	bswapl	%eax
	ret
}

_type_
__byte_swap_u16_variable(_type_)
{
	movl	4(%esp), %eax
	xchgb	%al, %ah
	ret
}
#endif

/*
 * void x86_flush()
 *
 * Flush instruction pipelines by doing an intersegment (far) return.
 */
void
x86_flush(void)
{
#if 0 // TODO: wasm fixme
	popl	%eax
	pushl	$GSEL(GCODE_SEL, SEL_KPL)
	pushl	%eax
	lret
#endif
    __panic_abort();
}

/* Waits - set up stack frame. */
void
x86_hlt(void)
{
#if 0 // TODO: wasm fixme
	pushl	%ebp
	movl	%esp, %ebp
	hlt
	leave
	ret
#endif
    __panic_abort();
}

/* Waits - set up stack frame. */
void
x86_stihlt(void)
{
#if 0 // TODO: wasm fixme
	pushl	%ebp
	movl	%esp, %ebp
	sti
	hlt
	leave
	ret
#endif
    __panic_abort();
}

void
x86_monitor(const void *addr, uint32_t flag, uint32_t hints)
{
#if 0 // TODO: wasm fixme
	movl	4(%esp), %eax
	movl	8(%esp), %ecx
	movl	12(%esp), %edx
	monitor	%eax, %ecx, %edx
	ret
#endif
    __panic_abort();
}

/* Waits - set up stack frame. */
void
x86_mwait(uint32_t state, uint32_t hints)
{
#if 0 // TODO: wasm fixme
	pushl	%ebp
	movl	%esp, %ebp
	movl	8(%ebp), %eax
	movl	12(%ebp), %ecx
	mwait	%eax, %ecx
	leave
	ret
#endif
    __panic_abort();
}  

void
stts(void)
{
#if 0 // TODO: wasm fixme
	movl	%cr0, %eax
	testl	$CR0_TS, %eax
	jnz	1f
	orl	$CR0_TS, %eax
	movl	%eax, %cr0
1:
	ret
#endif
    __panic_abort();
}

void
fldummy(void)
{
#if 0 // TODO: wasm fixme
	ffree	%st(7)
	fldz
	ret
#endif
    __panic_abort();
}

uint8_t
inb(uint32_t addr)
{
#if 0 // TODO: wasm fixme
	movl	4(%esp), %edx
	xorl	%eax, %eax
	inb	%dx, %al
	ret
#endif
    __panic_abort();
    return 0;
}

uint16_t
inw(uint32_t addr)
{
#if 0 // TODO: wasm fixme
	movl	4(%esp), %edx
	xorl	%eax, %eax
	inw	%dx, %ax
	ret
#endif
    __panic_abort();
    return 0;
}

uint32_t
inl(uint32_t addr)
{
#if 0 // TODO: wasm fixme
	movl	4(%esp), %edx
	inl	%dx, %eax
	ret
#endif
    __panic_abort();
    return 0;
}

void
outb(uint32_t addr, uint8_t c)
{
#if 0 // TODO: wasm fixme
	movl	4(%esp), %edx
	movl	8(%esp), %eax
	outb	%al, %dx
	ret
#endif
    __panic_abort();
}

void
outw(uint32_t addr, uint16_t c)
{
#if 0 // TODO: wasm fixme
	movl	4(%esp), %edx
	movl	8(%esp), %eax
	outw	%ax, %dx
	ret
#endif
    __panic_abort();
}

void
outl(uint32_t addr, uint32_t c)
{
#if 0 // TODO: wasm fixme
	movl	4(%esp), %edx
	movl	8(%esp), %eax
	outl	%eax, %dx
	ret
#endif
    __panic_abort();
}

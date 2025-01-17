/*	$NetBSD: i386func.S,v 1.22 2020/05/19 21:40:55 ad Exp $	*/

/*-
 * Copyright (c) 1998, 2007, 2008 The NetBSD Foundation, Inc.
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
 * These are _not_ shared with NetBSD/xen.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: i386func.S,v 1.22 2020/05/19 21:40:55 ad Exp $");

#include <sys/types.h>

#include <machine/specialreg.h>
#include <machine/segments.h>

#include <wasm/wasm-extra.h>

// https://www.felixcloutier.com/x86/invlpg
void
invlpg(vaddr_t va)
{
#if 0 // TODO: wasm fixme
	movl	4(%esp), %eax
	invlpg	(%eax)
	ret
#endif
    //__panic_abort();
}

// both lgdt and lldt has todo with virtual memory access
// https://pdos.csail.mit.edu/6.828/2011/readings/i386/s05_01.htm
void
lldt(u_short)
{
#if 0 // TODO: wasm fixme
	movl	4(%esp), %eax
	cmpl	%eax, CPUVAR(CURLDT)
	jne	1f
	ret
1:
	movl	%eax, CPUVAR(CURLDT)
	lldt	%ax
	ret
#endif
    //__panic_abort();
}

void
ltr(u_short)
{
#if 0 // TODO: wasm fixme
	movl	4(%esp), %eax
	ltr	%ax
	ret
	__panic_abort();
#endif
}

/*
 * Big hammer: flush all TLB entries, including ones from PTE's
 * with the G bit set.  This should only be necessary if TLB
 * shootdown falls far behind.
 *
 * Intel Architecture Software Developer's Manual, Volume 3,
 *	System Programming, section 9.10, "Invalidating the
 * Translation Lookaside Buffers (TLBS)":
 * "The following operations invalidate all TLB entries, irrespective
 * of the setting of the G flag:
 * ...
 * "(P6 family processors only): Writing to control register CR4 to
 * modify the PSE, PGE, or PAE flag."
 *
 * (the alternatives not quoted above are not an option here.)
 *
 * If PGE is not in use, we reload CR3.  Check for the PGE feature
 * first since i486 does not have CR4.  Note: the feature flag may
 * be present while the actual PGE functionality not yet enabled.
 */
void
tlbflushg(void)
{
#if 0 // TODO: wasm fixme
	testl	$CPUID_PGE, _C_LABEL(cpu_feature)
	jz	1f
	movl	%cr4, %eax
	testl	$CR4_PGE, %eax
	jz	1f
	movl	%eax, %edx
	andl	$~CR4_PGE, %edx
	movl	%edx, %cr4
	movl	%eax, %cr4
	ret
#endif
   	// do nothing
}

void
tlbflush(void)
{
#if 0 // TODO: wasm fixme
1:
	movl	%cr3, %eax
	movl	%eax, %cr3
	ret
#endif
    __panic_abort();
}

void
wbinvd(void)
{
#if 0 // TODO: wasm fixme
	wbinvd
	ret
#endif
    __panic_abort();
}

/*
 * void lgdt(struct region_descriptor *rdp);
 *
 * Load a new GDT pointer (and do any necessary cleanup).
 * XXX It's somewhat questionable whether reloading all the segment registers
 * is necessary, since the actual descriptor data is not changed except by
 * process creation and exit, both of which clean up via task switches.  OTOH,
 * this only happens at run time when the GDT is resized.
 */
void
lgdt(struct region_descriptor *rdp)
{
#if 0 // TODO: wasm fixme
	/* Reload the descriptor table. */
	movl	4(%esp), %eax
	lgdt	(%eax)
	/* Flush the prefetch queue. */
	jmp	1f
	nop
1:	/* Reload "stale" selectors. */
	movl	$GSEL(GDATA_SEL, SEL_KPL), %eax
	movl	%eax, %ds
	movl	%eax, %es
	movl	%eax, %gs
	movl	%eax, %ss
	movl	$GSEL(GCPU_SEL, SEL_KPL), %eax
	movl	%eax, %fs
	jmp	_C_LABEL(x86_flush)
#endif
    //__panic_abort();
	// wasm? whats is this for?
}

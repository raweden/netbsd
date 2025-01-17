/*	$NetBSD: compat___sigtramp1.c,v 1.3 2014/05/23 02:34:33 uebayasi Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
 * The wasm signal trampoline is invoked only to return from
 * the signal; the kernel calls the signal handler directly.
 *
 * On entry, stack looks like:
 *
 *		sigcontext structure			[12]
 *		pointer to sigcontext structure		[8]
 *		signal specific code			[4]
 *	sp->	signal number				[0]
 */
#include <math.h>
void
__wasm_sigtramp_sigcontext(void)
{
	
}

#if 0
NENTRY(__sigtramp_sigcontext_1)
	leal	12(%esp),%eax	/* get pointer to sigcontext */
	movl	%eax,4(%esp)	/* put it in the argument slot */
				/* fake return address already there */
	SYSTRAP(compat_16___sigreturn14) /* do sigreturn */
	movl	%eax,4(%esp)	/* error code */
	SYSTRAP(exit)		/* exit */
END(__sigtramp_sigcontext_1)
#endif


void
__wasm_sigtramp_siginfo(void)
{

}

void(*__sigtramp_sigcontext_1)(void) = __wasm_sigtramp_sigcontext;
void(*__sigtramp_siginfo_2)(void) = __wasm_sigtramp_siginfo;

#if 0
NENTRY(__sigtramp_siginfo_2)
	leal	12+128(%esp),%eax	/* get address of ucontext */
	movl	%eax,4(%esp)	/* put it in the argument slot */
				/* fake return address already there */
	SYSTRAP(setcontext)	/* do setcontext */
	movl	$-1,4(%esp)	/* if we return here, something is wrong */
	SYSTRAP(exit)		/* exit */
	.cfi_endproc
END(__sigtramp_siginfo_2)

#endif
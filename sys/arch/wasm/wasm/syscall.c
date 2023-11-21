/*	$NetBSD: syscall.c,v 1.21 2022/03/17 22:22:49 riastradh Exp $	*/

/*-
 * Copyright (c) 1998, 2000, 2009 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum.
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

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: syscall.c,v 1.21 2022/03/17 22:22:49 riastradh Exp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/ktrace.h>
#include <sys/syscall.h>
#include <sys/syscallvar.h>
#include <sys/syscall_stats.h>

#include <uvm/uvm_extern.h>

#include <machine/cpu.h>
#include <machine/psl.h>
#include <machine/userret.h>
#include <machine/frame.h>

#include "opt_dtrace.h"

#include <wasm/wasm_module.h>

void __panic_abort(void) __WASM_IMPORT(kern, panic_abort);

struct wasm_trapframe { 	// 280 bytes
    uint64_t tf_ra;
    uint64_t tf_sp;
    uint64_t tf_gp;
    uint64_t tf_tp;
    uint64_t tf_t[7];
    uint64_t tf_s[12];
    uint64_t tf_a[8];
    uint64_t tf_sepc;
    uint64_t tf_sstatus;
    uint64_t tf_stval;
    uint64_t tf_scause;
};

#ifndef __x86_64__
int		x86_copyargs(void *, void *, size_t);
#endif

void		syscall_intern(struct proc *);
static void	syscall(struct trapframe *);

void
md_child_return(struct lwp *l)
{
	struct trapframe *tf = l->l_md.md_regs;

	X86_TF_RAX(tf) = 0;
	X86_TF_RFLAGS(tf) &= ~PSL_C;

	userret(l);
}

/*
 * Process the tail end of a posix_spawn() for the child.
 */
void
cpu_spawn_return(struct lwp *l)
{

	userret(l);
}

/*
 * syscall(frame):
 *	System call request from POSIX system call gate interface to kernel.
 *	Like trap(), argument is call by reference.
 */
#ifdef KDTRACE_HOOKS
void syscall(struct trapframe *);
#else
static
#endif
void
syscall(struct trapframe *frame)
{
	struct wasm_trapframe *tf = (struct wasm_trapframe *)frame;
	const struct sysent *callp;
	struct proc *p;
	struct lwp *l;
	int error;
	register_t code, rval[2];
	register_t args[2 + SYS_MAXSYSARGS];

	l = (struct lwp *)curlwp;
	p = l->l_proc;
	LWP_CACHE_CREDS(l, p);

	code = (tf->tf_t[0]) & (SYS_NSYSENT - 1);
	callp = p->p_emul->e_sysent + code;

	SYSCALL_COUNT(syscall_counts, code);
	SYSCALL_TIME_SYS_ENTRY(l, syscall_times, code);

	if (callp->sy_argsize) {
		memcpy(args, tf->tf_a, sizeof(tf->tf_a));
	}

	error = sy_invoke(callp, l, args, rval, code);

	if (__predict_true(error == 0)) {
		tf->tf_a[0] = rval[0];
		tf->tf_a[1] = rval[1];
		tf->tf_t[0] &= ~PSL_C;	// carry bit
#if 0
		X86_TF_RAX(frame) = rval[0];
		X86_TF_RDX(frame) = rval[1];
		X86_TF_RFLAGS(frame) &= ~PSL_C;	/* carry bit */
#endif
	} else {
		tf->tf_a[0] = error;
		tf->tf_t[0] |= PSL_C; // carry bit
#if 0
		switch (error) {
		case ERESTART:
			/*
			 * The offset to adjust the PC by depends on whether we
			 * entered the kernel through the trap or call gate.
			 * We saved the instruction size in tf_err on entry.
			 */
			X86_TF_RIP(frame) -= frame->tf_err;
			break;
		case EJUSTRETURN:
			/* nothing to do */
			break;
		default:
		bad:
			X86_TF_RAX(frame) = error;
			X86_TF_RFLAGS(frame) |= PSL_C;	/* carry bit */
			break;
		}
#endif
	}

	SYSCALL_TIME_SYS_EXIT(l);
#ifndef __WASM
	userret(l);
#endif
}

void
syscall_intern(struct proc *p)
{

	p->p_md.md_syscall = syscall;
}

/**
 * Main entry-point for syscalls.
 */
void
syscall_trap_handler(struct wasm_trapframe *user_tf)
{
    struct lwp *l;
    struct wasm_trapframe kern_tf;
    l = (struct lwp *)curlwp;
	
    copyin(user_tf, &kern_tf, sizeof(struct wasm_trapframe));

    l->l_proc->p_md.md_syscall((struct trapframe *)&kern_tf);
    
	copyout(&kern_tf, user_tf, sizeof(struct wasm_trapframe));
}
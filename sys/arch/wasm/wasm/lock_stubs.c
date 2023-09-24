/*	$NetBSD: lock_stubs.S,v 1.38 2022/09/08 06:57:44 knakahara Exp $	*/

/*-
 * Copyright (c) 2006, 2007, 2008, 2009 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Andrew Doran.
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

 // based on sys/arch/i386/i386/lock_stubs.S

/*
 * Where possible we make each routine fit into an assumed 64-byte cache
 * line.  Please check alignment with 'objdump -d' after making changes.
 */


#include <stdint.h>
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: lock_stubs.S,v 1.38 2022/09/08 06:57:44 knakahara Exp $");

#include "opt_lockdebug.h"

#define __RWLOCK_PRIVATE
#define __MUTEX_PRIVATE

#include <sys/types.h>

#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <machine/cputypes.h>
#include <sys/cpu.h>

#include <wasm/wasm_module.h>
#include <wasm/wasm_inst.h>

void __panic_abort(void) __WASM_IMPORT(kern, panic_abort);

void __cpu_simple_lock(__cpu_simple_lock_t *lockp);
void __cpu_simple_unlock(__cpu_simple_lock_t *lockp);
int __cpu_simple_lock_try(__cpu_simple_lock_t *lockp);

#if 0
#define	ALIGN64		.align	64
#define	ALIGN32		.align	32
#define	LOCK(num)	\
	HOTPATCH(HP_NAME_NOLOCK, 1)	; \
	lock
#define	RET(num)	\
	HOTPATCH(HP_NAME_RETFENCE, 3)	; \
	ret; nop; nop			; \
	ret

#define	ENDLABEL(name,a) .align	a; LABEL(name)
#endif

#if !defined(LOCKDEBUG)

/*
 * void mutex_enter(kmutex_t *mtx);
 *
 * Acquire a mutex and post a load fence.
 */
void
mutex_enter(kmutex_t *mtx)
{
	if ((mtx->u.mtxa_owner & MUTEX_BIT_SPIN) != 0) {
		__cpu_simple_lock(&mtx->u.s.mtxs_lock);
		return;
	}

	uintptr_t lwp = (uintptr_t)curlwp;
	uintptr_t old;
	old = atomic_cmpxchg32(&mtx->u.mtxa_owner, 0, (uint32_t)lwp);
	if (old == 0 || old == lwp)
		return;
#if 0 // TODO: wasm fixme
	movl	4(%esp), %edx
	xorl	%eax, %eax
	movl	%fs:CPU_INFO_CURLWP(%eax), %ecx
	LOCK(1)
	cmpxchgl %ecx, (%edx)
	jnz	1f
	RET(1)
1:
	jmp	_C_LABEL(mutex_vector_enter)
#endif
    __panic_abort();
}

/*
 * void mutex_exit(kmutex_t *mtx);
 *
 * Release a mutex and post a load fence.
 *
 * See comments in mutex_vector_enter() about doing this operation unlocked
 * on multiprocessor systems, and comments in arch/x86/include/lock.h about
 * memory ordering on Intel x86 systems.
 */
void
mutex_exit(kmutex_t *mtx)
{
	if ((mtx->u.mtxa_owner & MUTEX_BIT_SPIN) != 0) {
		__cpu_simple_unlock(&mtx->u.s.mtxs_lock);
		return;
	}

	uintptr_t lwp = (uintptr_t)curlwp;
	uintptr_t old;
	old = atomic_cmpxchg32(&mtx->u.mtxa_owner, (uint32_t)lwp, 0);
	if (old == lwp)
		return;

#if 0 // TODO: wasm fixme
	movl	4(%esp), %edx
	xorl	%ecx, %ecx
	movl	%fs:CPU_INFO_CURLWP(%ecx), %eax
	cmpxchgl %ecx, (%edx)
	jnz	1f
	ret
1:
	jmp	_C_LABEL(mutex_vector_exit)
#endif
    __panic_abort();
}

/*
 * void rw_enter(krwlock_t *rwl, krw_t op);
 *
 * Acquire one hold on a RW lock.
 */
void
rw_enter(krwlock_t *rwl, krw_t op)
{
	// TODO: wasm; implement real rw locking..

#if 0 // TODO: wasm fixme
	movl	4(%esp), %edx
	cmpl	$RW_READER, 8(%esp)
	jne	2f

	/*
	 * Reader
	 */
	movl	(%edx), %eax
0:
	testb	$(RW_WRITE_LOCKED|RW_WRITE_WANTED), %al
	jnz	3f
	leal	RW_READ_INCR(%eax), %ecx
	LOCK(2)
	cmpxchgl %ecx, (%edx)
	jnz	1f
	RET(2)
1:
	jmp	0b

	/*
	 * Writer
	 */
2:	xorl	%eax, %eax
	movl	%fs:CPU_INFO_CURLWP(%eax), %ecx
	orl	$RW_WRITE_LOCKED, %ecx
	LOCK(3)
	cmpxchgl %ecx, (%edx)
	jnz	3f
	RET(3)
3:
	jmp	_C_LABEL(rw_vector_enter)
#endif
    //__panic_abort();
}

/*
 * void rw_exit(krwlock_t *rwl);
 *
 * Release one hold on a RW lock.
 */
void
rw_exit(krwlock_t *rwl)
{
	// TODO: wasm; implement real rw locking..

#if 0
	uintptr_t owner;
	uintptr_t newv;
	uintptr_t oldv;

	owner = atomic_load32(&rwl->rw_owner);

	if ((owner & RW_WRITE_LOCKED) != 0) {
		uintptr_t lwp = owner & RW_THREAD;
		if (lwp == (uintptr_t)curlwp) {
			newv = owner & ~RW_THREAD;
			oldv = atomic_cmpxchg32(&rwl->rw_owner, owner, newv);
			// this should not be possible..
		}
	}
#endif

#if 0 // TODO: wasm fixme
	movl	4(%esp), %edx
	movl	(%edx), %eax
	testb	$RW_WRITE_LOCKED, %al
	jnz	2f

	/*
	 * Reader
	 */
0:	testb	$RW_HAS_WAITERS, %al
	jnz	3f
	cmpl	$RW_READ_INCR, %eax
	jb	3f
	leal	-RW_READ_INCR(%eax), %ecx
	LOCK(4)
	cmpxchgl %ecx, (%edx)
	jnz	1f
	ret
1:
	jmp	0b

	/*
	 * Writer
	 */
2:	leal	-RW_WRITE_LOCKED(%eax), %ecx
	subl	CPUVAR(CURLWP), %ecx
	jnz	3f
	LOCK(5)
	cmpxchgl %ecx, (%edx)
	jnz	3f
	ret

	/*
	 * Slow path.
	 */
3:	jmp	_C_LABEL(rw_vector_exit)
#endif
}

/*
 * int rw_tryenter(krwlock_t *rwl, krw_t op);
 *
 * Try to acquire one hold on a RW lock.
 */
int
rw_tryenter(krwlock_t *rwl, krw_t op)
{
	// TODO: wasm; implement real rw locking..

#if 0 // TODO: wasm fixme
	movl	4(%esp), %edx
	cmpl	$RW_READER, 8(%esp)
	jne	2f

	/*
	 * Reader
	 */
	movl	(%edx), %eax
0:
	testb	$(RW_WRITE_LOCKED|RW_WRITE_WANTED), %al
	jnz	4f
	leal	RW_READ_INCR(%eax), %ecx
	LOCK(12)
	cmpxchgl %ecx, (%edx)
	jnz	1f
	movl	%edx, %eax			/* nonzero */
	RET(4)
1:
	jmp	0b

	/*
	 * Writer
	 */
2:
	xorl	%eax, %eax
	movl	%fs:CPU_INFO_CURLWP(%eax), %ecx
	orl	$RW_WRITE_LOCKED, %ecx
	LOCK(13)
	cmpxchgl %ecx, (%edx)
	movl	$0, %eax
	setz	%al
3:
	RET(5)
4:
	xorl	%eax, %eax
	jmp	3b
#endif
    //__panic_abort();
	return 0;
}


/*
 * void mutex_spin_enter(kmutex_t *mtx);
 *
 * Acquire a spin mutex and post a load fence.
 */
void
mutex_spin_enter(kmutex_t *mtx)
{
#if 0 // TODO: wasm fixme
	movl	4(%esp), %edx
	movb	CPUVAR(ILEVEL), %cl
	movb	MTX_IPL(%edx), %ch
	movl	$0x01, %eax
	cmpb	%ch, %cl
	jg	1f
	movb	%ch, CPUVAR(ILEVEL)		/* splraiseipl() */
1:
	subl	%eax, CPUVAR(MTX_COUNT)		/* decl does not set CF */
	jnc	2f
	movb	%cl, CPUVAR(MTX_OLDSPL)
2:
	xchgb	%al, MTX_LOCK(%edx)		/* lock it */
	testb	%al, %al
	jnz	3f
	RET(6)
3:
	jmp	_C_LABEL(mutex_spin_retry)

	ALIGN64
LABEL(mutex_spin_enter_end)
#endif

	// TODO: wasm; fix spin lock

    //__panic_abort();
}

#ifndef XENPV
/*
 * Release a spin mutex and post a store fence. Must occupy 128 bytes.
 */
void
mutex_spin_exit(kmutex_t *mtx)
{
#if 0 // TODO: wasm fixme
	HOTPATCH(HP_NAME_MUTEX_EXIT, 128)
	movl	4(%esp), %edx
	movl	CPUVAR(MTX_OLDSPL), %ecx
	incl	CPUVAR(MTX_COUNT)
	movb	$0, MTX_LOCK(%edx)		/* zero */
	jnz	1f
	movl	CPUVAR(IUNMASK)(,%ecx,8), %edx
	movl	CPUVAR(IUNMASK)+4(,%ecx,8), %eax
	cli
	testl	CPUVAR(IPENDING), %edx
	movl    %ecx, 4(%esp)
	jnz	_C_LABEL(Xspllower)		/* does sti */
	testl	CPUVAR(IPENDING)+4, %eax
	jnz	_C_LABEL(Xspllower)		/* does sti */
	movb	%cl, CPUVAR(ILEVEL)
	sti
1:	ret
	.space	32, 0xCC
	.align	32
#endif
    //__panic_abort();

	// TODO: wasm; fix spin lock
}
#else  /* XENPV */
__t__ mutex_spin_exit(__t__) __attribute__((alias("i686_mutex_spin_exit")))
#endif	/* !XENPV */

/*
 * Patch for i686 CPUs where cli/sti is prohibitively expensive.
 * Must be the same size as mutex_spin_exit(), that is, 128 bytes.
 */
void
i686_mutex_spin_exit(kmutex_t *mtx)
{
#if 0 // TODO: wasm fixme
	mov	4(%esp),%edx
	movl	CPUVAR(MTX_OLDSPL), %ecx
	incl	CPUVAR(MTX_COUNT)
	movb	$0, MTX_LOCK(%edx)		/* zero */
	jnz	1f
	pushl	%ebx
	pushl	%esi
	pushl	%edi
	movl	%ecx, %esi
	movl	%ecx, %edi
	shll	$24, %edi
0:
	movl	CPUVAR(IPENDING), %eax
	testl	%eax, CPUVAR(IUNMASK)(,%esi,8)
	jnz	2f
	movl	CPUVAR(IPENDING)+4, %edx
	testl	%edx, CPUVAR(IUNMASK)+4(,%esi,8)
	jnz	2f
	movl	%eax, %ebx
	movl	%edx, %ecx
	andl	$0x00ffffff, %ecx
	orl	%edi, %ecx
	cmpxchg8b CPUVAR(ISTATE)		/* swap in new ilevel */
	jnz	0b
	popl	%edi
	popl	%esi
	popl	%ebx
1:
	ret
2:
	movl	%esi,%ecx
	popl	%edi
	popl	%esi
	popl	%ebx
	movl	%ecx,4(%esp)

	/* The reference must be absolute, hence the indirect jump. */
	movl	$Xspllower,%eax
	jmp	*%eax

	.space	16, 0xCC
	.align	32
LABEL(i686_mutex_spin_exit_end)
#endif
    __panic_abort();
}

#endif	/* !LOCKDEBUG */

/*
 * Spinlocks.
 */
void
__cpu_simple_lock_init(__cpu_simple_lock_t *lockp)
{
	*lockp = 0;
}

void
__cpu_simple_lock(__cpu_simple_lock_t *lockp)
{
	uint32_t count = 0;
	uint8_t old;
	old = atomic_cmpxchg8(lockp, 0, 1);
	if (old == 0)
		return;
	
	while (true) {
		wasm_inst_nop();
		wasm_inst_nop();
		old = atomic_cmpxchg8(lockp, 0, 1);
		if (old == 0)
			return;
		if (count > 0xf00000)
			__panic_abort();
		count++;
	}

#if 0 // TODO: wasm fixme
	movl	4(%esp), %edx
	movl	$0x0100, %eax
1:
	LOCK(6)
	cmpxchgb %ah, (%edx)
	jnz	2f
	RET(7)
2:
	movl	$0x0100, %eax
	pause
	nop
	nop
	cmpb	$0, (%edx)
	je	1b
	jmp	2b
#endif
    __panic_abort();
}

void
__cpu_simple_unlock(__cpu_simple_lock_t *lockp)
{
	atomic_store8(lockp, 0);
}

int
__cpu_simple_lock_try(__cpu_simple_lock_t *lockp)
{
	uint8_t old;
	old = atomic_cmpxchg8(lockp, 0, 1);
	return old == 0 ? true : false;
}


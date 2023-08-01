/*	$NetBSD: trap.c,v 1.21 2023/05/07 12:41:49 skrll Exp $	*/

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas of 3am Software Foundry.
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

#define	__PMAP_PRIVATE
#define	__UFETCHSTORE_PRIVATE

__RCSID("$NetBSD: trap.c,v 1.21 2023/05/07 12:41:49 skrll Exp $");

#include <sys/param.h>

#include <sys/atomic.h>
#include <sys/kauth.h>
#include <sys/kmem.h>
#include <sys/signal.h>
#include <sys/signalvar.h>
#include <sys/siginfo.h>
#include <sys/systm.h>
#include <sys/lwp.h>

#include <uvm/uvm.h>

#include <machine/userret.h>
#include <machine/machdep.h>
#include <machine/db_machdep.h>

#define	MACHINE_ECALL_TRAP_MASK	(__BIT(CAUSE_MACHINE_ECALL))

#define	SUPERVISOR_ECALL_TRAP_MASK					\
				(__BIT(CAUSE_SUPERVISOR_ECALL))

#define	USER_ECALL_TRAP_MASK	(__BIT(CAUSE_USER_ECALL))

#define	SYSCALL_TRAP_MASK	(__BIT(CAUSE_SYSCALL))

#define	BREAKPOINT_TRAP_MASK	(__BIT(CAUSE_BREAKPOINT))

#define	INSTRUCTION_TRAP_MASK	(__BIT(CAUSE_ILLEGAL_INSTRUCTION))

#define	FAULT_TRAP_MASK		(__BIT(CAUSE_FETCH_ACCESS) 		\
				|__BIT(CAUSE_LOAD_ACCESS) 		\
				|__BIT(CAUSE_STORE_ACCESS)		\
				|__BIT(CAUSE_FETCH_PAGE_FAULT) 		\
				|__BIT(CAUSE_LOAD_PAGE_FAULT) 		\
				|__BIT(CAUSE_STORE_PAGE_FAULT))

#define	MISALIGNED_TRAP_MASK	(__BIT(CAUSE_FETCH_MISALIGNED)		\
				|__BIT(CAUSE_LOAD_MISALIGNED)		\
				|__BIT(CAUSE_STORE_MISALIGNED))



#if 0
int
copyin(const void *uaddr, void *kaddr, size_t len)
{
	struct faultbuf fb;
	int error;

	if (__predict_false(len == 0)) {
		return 0;
	}

	// XXXNH cf. VM_MIN_ADDRESS and user_va0_disable
	if (uaddr == NULL)
		return EFAULT;

	const vaddr_t uva = (vaddr_t)uaddr;
	if (uva > VM_MAXUSER_ADDRESS - len)
		return EFAULT;

	csr_sstatus_set(SR_SUM);
	if ((error = cpu_set_onfault(&fb, EFAULT)) == 0) {
		memcpy(kaddr, uaddr, len);
		cpu_unset_onfault();
	}
	csr_sstatus_clear(SR_SUM);

	return error;
}

int
copyout(const void *kaddr, void *uaddr, size_t len)
{
	struct faultbuf fb;
	int error;

	if (__predict_false(len == 0)) {
		return 0;
	}

	// XXXNH cf. VM_MIN_ADDRESS and user_va0_disable
	if (uaddr == NULL)
		return EFAULT;

	const vaddr_t uva = (vaddr_t)uaddr;
	if (uva > VM_MAXUSER_ADDRESS - len)
		return EFAULT;

	csr_sstatus_set(SR_SUM);
	if ((error = cpu_set_onfault(&fb, EFAULT)) == 0) {
		memcpy(uaddr, kaddr, len);
		cpu_unset_onfault();
	}
	csr_sstatus_clear(SR_SUM);

	return error;
}
#endif

int
kcopy(const void *kfaddr, void *kdaddr, size_t len)
{
#if 0
	struct faultbuf fb;
	int error;

	if ((error = cpu_set_onfault(&fb, EFAULT)) == 0) {
		memcpy(kdaddr, kfaddr, len);
		cpu_unset_onfault();
	}

	return error;
#endif
	memcpy(kdaddr, kfaddr, len);
	return (0);
}

#if 0
int
copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done)
{
	struct faultbuf fb;
	size_t retlen;
	int error;

	if (__predict_false(len == 0)) {
		return 0;
	}

	if (__predict_false(uaddr == NULL))
		return EFAULT;
	/*
	 * Can only check if starting user address is out of range here.
	 * The string may end before uva + len.
	 */
	const vaddr_t uva = (vaddr_t)uaddr;
	if (uva > VM_MAXUSER_ADDRESS)
		return EFAULT;

	csr_sstatus_set(SR_SUM);
	if ((error = cpu_set_onfault(&fb, EFAULT)) == 0) {
		retlen = strlcpy(kaddr, uaddr, len);
		cpu_unset_onfault();
		if (retlen >= len) {
			error = ENAMETOOLONG;
		} else if (done != NULL) {
			*done = retlen + 1;
		}
	}
	csr_sstatus_clear(SR_SUM);

	return error;
}

int
copyoutstr(const void *kaddr, void *uaddr, size_t len, size_t *done)
{
	struct faultbuf fb;
	size_t retlen;
	int error;

	if (__predict_false(len == 0)) {
		return 0;
	}

	if (__predict_false(uaddr == NULL))
		return EFAULT;
	/*
	 * Can only check if starting user address is out of range here.
	 * The string may end before uva + len.
	 */
	const vaddr_t uva = (vaddr_t)uaddr;
	if (uva > VM_MAXUSER_ADDRESS)
		return EFAULT;

	csr_sstatus_set(SR_SUM);
	if ((error = cpu_set_onfault(&fb, EFAULT)) == 0) {
		retlen = strlcpy(uaddr, kaddr, len);
		cpu_unset_onfault();
		if (retlen >= len) {
			error = ENAMETOOLONG;
		} else if (done != NULL) {
			*done = retlen + 1;
		}
	}
	csr_sstatus_clear(SR_SUM);

	return error;
}
#endif

#if 0
static int
fetch_user_data(const void *uaddr, void *valp, size_t size)
{
	struct faultbuf fb;
	int error;

	const vaddr_t uva = (vaddr_t)uaddr;
	if (__predict_false(uva > VM_MAXUSER_ADDRESS - size))
		return EFAULT;

	if ((error = cpu_set_onfault(&fb, 1)) != 0)
		return error;

	csr_sstatus_set(SR_SUM);
	switch (size) {
	case 1:
		*(uint8_t *)valp = *(volatile const uint8_t *)uaddr;
		break;
	case 2:
		*(uint16_t *)valp = *(volatile const uint16_t *)uaddr;
		break;
	case 4:
		*(uint32_t *)valp = *(volatile const uint32_t *)uaddr;
		break;
#ifdef _LP64
	case 8:
		*(uint64_t *)valp = *(volatile const uint64_t *)uaddr;
		break;
#endif /* _LP64 */
	default:
		error = EINVAL;
	}
	csr_sstatus_clear(SR_SUM);

	cpu_unset_onfault();

	return error;
}
#endif

#if 0
static int
store_user_data(void *uaddr, const void *valp, size_t size)
{
	struct faultbuf fb;
	int error;

	const vaddr_t uva = (vaddr_t)uaddr;
	if (__predict_false(uva > VM_MAXUSER_ADDRESS - size))
		return EFAULT;

	if ((error = cpu_set_onfault(&fb, 1)) != 0)
		return error;

	csr_sstatus_set(SR_SUM);
	switch (size) {
	case 1:
		*(volatile uint8_t *)uaddr = *(const uint8_t *)valp;
		break;
	case 2:
		*(volatile uint16_t *)uaddr = *(const uint8_t *)valp;
		break;
	case 4:
		*(volatile uint32_t *)uaddr = *(const uint32_t *)valp;
		break;
#ifdef _LP64
	case 8:
		*(volatile uint64_t *)uaddr = *(const uint64_t *)valp;
		break;
#endif /* _LP64 */
	default:
		error = EINVAL;
	}
	csr_sstatus_clear(SR_SUM);

	cpu_unset_onfault();

	return error;
}
#endif


// i386 start

/* 
 * startlwp: start of a new LWP.
 */
void
startlwp(void *arg)
{
	ucontext_t *uc = arg;
	lwp_t *l = curlwp;
	int error __diagused;

	error = cpu_setmcontext(l, &uc->uc_mcontext, uc->uc_flags);
	KASSERT(error == 0);

	kmem_free(uc, sizeof(ucontext_t));
	userret(l);
}

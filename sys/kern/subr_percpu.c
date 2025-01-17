/*	$NetBSD: subr_percpu.c,v 1.25 2020/05/11 21:37:31 riastradh Exp $	*/

/*-
 * Copyright (c)2007,2008 YAMAMOTO Takashi,
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * per-cpu storage.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: subr_percpu.c,v 1.25 2020/05/11 21:37:31 riastradh Exp $");

#include <sys/param.h>
#include <sys/cpu.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/percpu.h>
#include <sys/rwlock.h>
#include <sys/vmem.h>
#include <sys/xcall.h>

#include <wasm/../mm/mm.h>

#define	PERCPU_QUANTUM_SIZE	(ALIGNBYTES + 1)
#define	PERCPU_QCACHE_MAX	0
#define	PERCPU_IMPORT_SIZE	2048

struct percpu {
	unsigned		pc_offset;
	size_t			pc_size;
	percpu_callback_t	pc_ctor;
	percpu_callback_t	pc_dtor;
	void			*pc_cookie;
	LIST_ENTRY(percpu)	pc_list;
};

static krwlock_t	    percpu_swap_lock	__cacheline_aligned;
static struct mm_arena *percpu_offset_arena	__read_mostly;
static struct {
	kmutex_t	lock;
	unsigned int	nextoff;
	LIST_HEAD(, percpu) ctor_list;
	struct lwp	*busy;
	kcondvar_t	cv;
} percpu_allocation __cacheline_aligned;

static percpu_cpu_t *
cpu_percpu(struct cpu_info *ci)
{

	return &ci->ci_data.cpu_percpu;
}

static unsigned int
percpu_offset(percpu_t *pc)
{
	const unsigned int off = pc->pc_offset;

	KASSERT(off < percpu_allocation.nextoff);
	return off;
}

/*
 * percpu_cpu_swap: crosscall handler for percpu_cpu_enlarge
 */
__noubsan
static void
percpu_cpu_swap(void *p1, void *p2)
{
	struct cpu_info * const ci = p1;
	percpu_cpu_t * const newpcc = p2;
	percpu_cpu_t * const pcc = cpu_percpu(ci);

	KASSERT(ci == curcpu() || !mp_online);

	/*
	 * swap *pcc and *newpcc unless anyone has beaten us.
	 */
	rw_enter(&percpu_swap_lock, RW_WRITER);
	if (newpcc->pcc_size > pcc->pcc_size) {
		percpu_cpu_t tmp;
		int s;

		tmp = *pcc;

		/*
		 * block interrupts so that we don't lose their modifications.
		 */

		s = splhigh();

		/*
		 * copy data to new storage.
		 */

		memcpy(newpcc->pcc_data, pcc->pcc_data, pcc->pcc_size);

		/*
		 * this assignment needs to be atomic for percpu_getptr_remote.
		 */

		pcc->pcc_data = newpcc->pcc_data;

		splx(s);

		pcc->pcc_size = newpcc->pcc_size;
		*newpcc = tmp;
	}
	rw_exit(&percpu_swap_lock);
}

/*
 * percpu_cpu_enlarge: ensure that percpu_cpu_t of each cpus have enough space
 */

static void
percpu_cpu_enlarge(size_t size)
{
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;

	for (CPU_INFO_FOREACH(cii, ci)) {
		percpu_cpu_t pcc;

		pcc.pcc_data = kmem_alloc(size, KM_SLEEP); /* XXX cacheline */
		pcc.pcc_size = size;
		if (!mp_online) {
			percpu_cpu_swap(ci, &pcc);
		} else {
			uint64_t where;

			where = xc_unicast(0, percpu_cpu_swap, ci, &pcc, ci);
			xc_wait(where);
		}
		KASSERT(pcc.pcc_size <= size);
		if (pcc.pcc_data != NULL) {
			kmem_free(pcc.pcc_data, pcc.pcc_size);
		}
	}
}

/*
 * percpu_backend_alloc: vmem import callback for percpu_offset_arena
 */

static int
percpu_backend_alloc(vmem_t *dummy, vmem_size_t size, vmem_size_t *resultsize,
    vm_flag_t vmflags, vmem_addr_t *addrp)
{
	unsigned int offset;
	unsigned int nextoff;

	ASSERT_SLEEPABLE();
	KASSERT(dummy == NULL);

	if ((vmflags & VM_NOSLEEP) != 0)
		return ENOMEM;

	size = roundup(size, PERCPU_IMPORT_SIZE);
	mutex_enter(&percpu_allocation.lock);
	offset = percpu_allocation.nextoff;
	percpu_allocation.nextoff = nextoff = percpu_allocation.nextoff + size;
	mutex_exit(&percpu_allocation.lock);

	percpu_cpu_enlarge(nextoff);

	*resultsize = size;
	*addrp = (vmem_addr_t)offset;
	return 0;
}

static void
percpu_zero_cb(void *vp, void *vp2, struct cpu_info *ci)
{
	size_t sz = (uintptr_t)vp2;

	memset(vp, 0, sz);
}

/*
 * percpu_zero: initialize percpu storage with zero.
 */

static void
percpu_zero(percpu_t *pc, size_t sz)
{

	percpu_foreach(pc, percpu_zero_cb, (void *)(uintptr_t)sz);
}

/*
 * percpu_init: subsystem initialization
 */

void
percpu_init(void)
{

	ASSERT_SLEEPABLE();
	rw_init(&percpu_swap_lock);
	mutex_init(&percpu_allocation.lock, MUTEX_DEFAULT, IPL_NONE);
	percpu_allocation.nextoff = PERCPU_QUANTUM_SIZE;
	LIST_INIT(&percpu_allocation.ctor_list);
	percpu_allocation.busy = NULL;
	cv_init(&percpu_allocation.cv, "percpu");

	percpu_offset_arena = mm_arena_xcreate("percpu", 0, 0, PERCPU_QUANTUM_SIZE,
	    percpu_backend_alloc, NULL, NULL, PERCPU_QCACHE_MAX, VM_SLEEP,
	    IPL_NONE);
}

/*
 * percpu_init_cpu: cpu initialization
 *
 * => should be called before the cpu appears on the list for CPU_INFO_FOREACH.
 * => may be called for static CPUs afterward (typically just primary CPU)
 */

void
percpu_init_cpu(struct cpu_info *ci)
{
	percpu_cpu_t * const pcc = cpu_percpu(ci);
	struct percpu *pc;
	size_t size = percpu_allocation.nextoff; /* XXX racy */

	ASSERT_SLEEPABLE();

	/*
	 * For the primary CPU, prior percpu_create may have already
	 * triggered allocation, so there's nothing more for us to do
	 * here.
	 */
	if (pcc->pcc_size)
		return;
	KASSERT(pcc->pcc_data == NULL);

	/*
	 * Otherwise, allocate storage and, while the constructor list
	 * is locked, run constructors for all percpus on this CPU.
	 */
	pcc->pcc_size = size;
	if (size) {
		pcc->pcc_data = kmem_zalloc(pcc->pcc_size, KM_SLEEP);
		mutex_enter(&percpu_allocation.lock);
		while (percpu_allocation.busy)
			cv_wait(&percpu_allocation.cv,
			    &percpu_allocation.lock);
		percpu_allocation.busy = curlwp;
		LIST_FOREACH(pc, &percpu_allocation.ctor_list, pc_list) {
			KASSERT(pc->pc_ctor);
			mutex_exit(&percpu_allocation.lock);
			(*pc->pc_ctor)((char *)pcc->pcc_data + pc->pc_offset,
			    pc->pc_cookie, ci);
			mutex_enter(&percpu_allocation.lock);
		}
		KASSERT(percpu_allocation.busy == curlwp);
		percpu_allocation.busy = NULL;
		cv_broadcast(&percpu_allocation.cv);
		mutex_exit(&percpu_allocation.lock);
	}
}

/*
 * percpu_alloc: allocate percpu storage
 *
 * => called in thread context.
 * => considered as an expensive and rare operation.
 * => allocated storage is initialized with zeros.
 */

percpu_t *
percpu_alloc(size_t size)
{

	return percpu_create(size, NULL, NULL, NULL);
}

/*
 * percpu_create: allocate percpu storage and associate ctor/dtor with it
 *
 * => called in thread context.
 * => considered as an expensive and rare operation.
 * => allocated storage is initialized by ctor, or zeros if ctor is null
 * => percpu_free will call dtor first, if dtor is nonnull
 * => ctor or dtor may sleep, even on allocation
 */

percpu_t *
percpu_create(size_t size, percpu_callback_t ctor, percpu_callback_t dtor,
    void *cookie)
{
	vmem_addr_t offset;
	percpu_t *pc;

	ASSERT_SLEEPABLE();
	(void)mm_arena_alloc(percpu_offset_arena, size, VM_SLEEP | VM_BESTFIT,
	    &offset);

	pc = kmem_alloc(sizeof(*pc), KM_SLEEP);
	pc->pc_offset = offset;
	pc->pc_size = size;
	pc->pc_ctor = ctor;
	pc->pc_dtor = dtor;
	pc->pc_cookie = cookie;

	if (ctor) {
		CPU_INFO_ITERATOR cii;
		struct cpu_info *ci;
		void *buf;

		/*
		 * Wait until nobody is using the list of percpus with
		 * constructors.
		 */
		mutex_enter(&percpu_allocation.lock);
		while (percpu_allocation.busy)
			cv_wait(&percpu_allocation.cv,
			    &percpu_allocation.lock);
		percpu_allocation.busy = curlwp;
		mutex_exit(&percpu_allocation.lock);

		/*
		 * Run the constructor for all CPUs.  We use a
		 * temporary buffer wo that we need not hold the
		 * percpu_swap_lock while running the constructor.
		 */
		buf = kmem_alloc(size, KM_SLEEP);
		for (CPU_INFO_FOREACH(cii, ci)) {
			memset(buf, 0, size);
			(*ctor)(buf, cookie, ci);
			percpu_traverse_enter();
			memcpy(percpu_getptr_remote(pc, ci), buf, size);
			percpu_traverse_exit();
		}
		explicit_memset(buf, 0, size);
		kmem_free(buf, size);

		/*
		 * Insert the percpu into the list of percpus with
		 * constructors.  We are now done using the list, so it
		 * is safe for concurrent percpu_create or concurrent
		 * percpu_init_cpu to run.
		 */
		mutex_enter(&percpu_allocation.lock);
		KASSERT(percpu_allocation.busy == curlwp);
		percpu_allocation.busy = NULL;
		cv_broadcast(&percpu_allocation.cv);
		LIST_INSERT_HEAD(&percpu_allocation.ctor_list, pc, pc_list);
		mutex_exit(&percpu_allocation.lock);
	} else {
		percpu_zero(pc, size);
	}

	return pc;
}

/*
 * percpu_free: free percpu storage
 *
 * => called in thread context.
 * => considered as an expensive and rare operation.
 */

void
percpu_free(percpu_t *pc, size_t size)
{

	ASSERT_SLEEPABLE();
	KASSERT(size == pc->pc_size);

	/*
	 * If there's a constructor, take the percpu off the list of
	 * percpus with constructors, but first wait until nobody is
	 * using the list.
	 */
	if (pc->pc_ctor) {
		mutex_enter(&percpu_allocation.lock);
		while (percpu_allocation.busy)
			cv_wait(&percpu_allocation.cv,
			    &percpu_allocation.lock);
		LIST_REMOVE(pc, pc_list);
		mutex_exit(&percpu_allocation.lock);
	}

	/* If there's a destructor, run it now for all CPUs.  */
	if (pc->pc_dtor) {
		CPU_INFO_ITERATOR cii;
		struct cpu_info *ci;
		void *buf;

		buf = kmem_alloc(size, KM_SLEEP);
		for (CPU_INFO_FOREACH(cii, ci)) {
			percpu_traverse_enter();
			memcpy(buf, percpu_getptr_remote(pc, ci), size);
			explicit_memset(percpu_getptr_remote(pc, ci), 0, size);
			percpu_traverse_exit();
			(*pc->pc_dtor)(buf, pc->pc_cookie, ci);
		}
		explicit_memset(buf, 0, size);
		kmem_free(buf, size);
	}

	mm_arena_free(percpu_offset_arena, (vmem_addr_t)percpu_offset(pc), size);
	kmem_free(pc, sizeof(*pc));
}

/*
 * percpu_getref:
 *
 * => safe to be used in either thread or interrupt context
 * => disables preemption; must be bracketed with a percpu_putref()
 */

void *
percpu_getref(percpu_t *pc)
{

	kpreempt_disable();
	return percpu_getptr_remote(pc, curcpu());
}

/*
 * percpu_putref:
 *
 * => drops the preemption-disabled count after caller is done with per-cpu
 *    data
 */

void
percpu_putref(percpu_t *pc)
{

	kpreempt_enable();
}

/*
 * percpu_traverse_enter, percpu_traverse_exit, percpu_getptr_remote:
 * helpers to access remote cpu's percpu data.
 *
 * => called in thread context.
 * => percpu_traverse_enter can block low-priority xcalls.
 * => typical usage would be:
 *
 *	sum = 0;
 *	percpu_traverse_enter();
 *	for (CPU_INFO_FOREACH(cii, ci)) {
 *		unsigned int *p = percpu_getptr_remote(pc, ci);
 *		sum += *p;
 *	}
 *	percpu_traverse_exit();
 */

void
percpu_traverse_enter(void)
{

	ASSERT_SLEEPABLE();
	rw_enter(&percpu_swap_lock, RW_READER);
}

void
percpu_traverse_exit(void)
{

	rw_exit(&percpu_swap_lock);
}

void *
percpu_getptr_remote(percpu_t *pc, struct cpu_info *ci)
{

	return &((char *)cpu_percpu(ci)->pcc_data)[percpu_offset(pc)];
}

/*
 * percpu_foreach: call the specified callback function for each cpus.
 *
 * => must be called from thread context.
 * => callback executes on **current** CPU (or, really, arbitrary CPU,
 *    in case of preemption)
 * => caller should not rely on the cpu iteration order.
 * => the callback function should be minimum because it is executed with
 *    holding a global lock, which can block low-priority xcalls.
 *    eg. it's illegal for a callback function to sleep for memory allocation.
 */
void
percpu_foreach(percpu_t *pc, percpu_callback_t cb, void *arg)
{
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;

	percpu_traverse_enter();
	for (CPU_INFO_FOREACH(cii, ci)) {
		(*cb)(percpu_getptr_remote(pc, ci), arg, ci);
	}
	percpu_traverse_exit();
}

struct percpu_xcall_ctx {
	percpu_callback_t  ctx_cb;
	void		  *ctx_arg;
};

static void
percpu_xcfunc(void * const v1, void * const v2)
{
	percpu_t * const pc = v1;
	struct percpu_xcall_ctx * const ctx = v2;

	(*ctx->ctx_cb)(percpu_getref(pc), ctx->ctx_arg, curcpu());
	percpu_putref(pc);
}

/*
 * percpu_foreach_xcall: call the specified callback function for each
 * cpu.  This version uses an xcall to run the callback on each cpu.
 *
 * => must be called from thread context.
 * => callback executes on **remote** CPU in soft-interrupt context
 *    (at the specified soft interrupt priority).
 * => caller should not rely on the cpu iteration order.
 * => the callback function should be minimum because it may be
 *    executed in soft-interrupt context.  eg. it's illegal for
 *    a callback function to sleep for memory allocation.
 */
void
percpu_foreach_xcall(percpu_t *pc, u_int xcflags, percpu_callback_t cb,
		     void *arg)
{
	struct percpu_xcall_ctx ctx = {
		.ctx_cb = cb,
		.ctx_arg = arg,
	};
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;

	for (CPU_INFO_FOREACH(cii, ci)) {
		xc_wait(xc_unicast(xcflags, percpu_xcfunc, pc, &ctx, ci));
	}
}

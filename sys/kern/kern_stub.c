/*	$NetBSD: kern_stub.c,v 1.50 2020/08/01 02:04:55 riastradh Exp $	*/

/*-
 * Copyright (c) 2007, 2008 The NetBSD Foundation, Inc.
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
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)subr_xxx.c	8.3 (Berkeley) 3/29/95
 */

/*
 * Stubs for system calls and facilities not included in the system.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: kern_stub.c,v 1.50 2020/08/01 02:04:55 riastradh Exp $");

#ifdef _KERNEL_OPT
#include "opt_ktrace.h"
#include "opt_sysv.h"
#include "opt_modular.h"
#endif

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/fstypes.h>
#include <sys/signalvar.h>
#include <sys/syscall.h>
#include <sys/ktrace.h>
#include <sys/intr.h>
#include <sys/cpu.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/userconf.h>

bool default_bus_space_is_equal(bus_space_tag_t, bus_space_tag_t);
bool default_bus_space_handle_is_equal(bus_space_tag_t, bus_space_handle_t,
    bus_space_handle_t);

/*
 * SYSV Semaphores, Shared Memory, Message Queues
 */
#ifndef MODULAR
#ifndef SYSVMSG
#ifndef __WASM
__strong_alias(msgctl1,enosys);
#endif
#endif
#ifndef SYSVSHM
#ifndef __WASM
__strong_alias(shmctl1,enosys);
#endif
#endif
#ifndef SYSVSEM
#ifndef __WASM
__strong_alias(semctl1,enosys);
#endif
#endif
#endif

/*
 * ktrace stubs.  ktruser() goes to enosys as we want to fail the syscall,
 * but not kill the process: utrace() is a debugging feature.
 */
#ifndef KTRACE
#ifndef __WASM
__strong_alias(ktr_csw,nullop);		/* Probes */
__strong_alias(ktr_emul,nullop);
__strong_alias(ktr_geniov,nullop);
__strong_alias(ktr_genio,nullop);
__strong_alias(ktr_mibio,nullop);
__strong_alias(ktr_namei,nullop);
__strong_alias(ktr_namei2,nullop);
__strong_alias(ktr_psig,nullop);
__strong_alias(ktr_syscall,nullop);
__strong_alias(ktr_sysret,nullop);
__strong_alias(ktr_kuser,nullop);
__strong_alias(ktr_mib,nullop);
__strong_alias(ktr_execarg,nullop);
__strong_alias(ktr_execenv,nullop);
__strong_alias(ktr_execfd,nullop);

__strong_alias(sys_fktrace,sys_nosys);	/* Syscalls */
__strong_alias(sys_ktrace,sys_nosys);
__strong_alias(sys_utrace,sys_nosys);

int	ktrace_on;			/* Misc */
__strong_alias(ktruser,enosys);
__strong_alias(ktr_point,nullop);
#else
// TODO: might need to have dedicated calls for these
void ktr_csw(int, int, const struct syncobj *) __attribute__((alias("nullop")));
void ktr_emul(void) __attribute__((alias("nullop")));
void ktr_geniov(int, enum uio_rw, struct iovec *, size_t, int) __attribute__((alias("nullop")));
void ktr_genio(int, enum uio_rw, const void *, size_t, int) __attribute__((alias("nullop")));
void ktr_mibio(int, enum uio_rw, const void *, size_t, int) __attribute__((alias("nullop")));
void ktr_namei(const char *, size_t) __attribute__((alias("nullop")));
void ktr_namei2(const char *, size_t, const char *, size_t) __attribute__((alias("nullop")));
void ktr_psig(int, sig_t, const sigset_t *, const ksiginfo_t *) __attribute__((alias("nullop")));
void ktr_syscall(register_t, const register_t [], int) __attribute__((alias("nullop")));
void ktr_sysret(register_t, int, register_t *) __attribute__((alias("nullop")));
void ktr_kuser(const char *, const void *, size_t) __attribute__((alias("nullop")));
void ktr_mib(const int *a , u_int b) __attribute__((alias("nullop")));
void ktr_execarg(const void *, size_t) __attribute__((alias("nullop")));
void ktr_execenv(const void *, size_t) __attribute__((alias("nullop")));
void ktr_execfd(int, u_int) __attribute__((alias("nullop")));

int sys_fktrace(void *) __attribute__((alias("sys_nosys")));
int sys_ktrace(void *) __attribute__((alias("sys_nosys")));
int sys_utrace(void *) __attribute__((alias("sys_nosys")));

int ktruser(const char *, void *, size_t, int) __attribute__((alias("enosys")));
bool ktr_point(int) __attribute__((alias("nullop")));
#endif
#endif	/* KTRACE */

#ifndef __WASM
__weak_alias(device_register, voidop);
__weak_alias(device_register_post_config, voidop);
__weak_alias(spldebug_start, voidop);
__weak_alias(spldebug_stop, voidop);
__weak_alias(machdep_init,nullop);
__weak_alias(pci_chipset_tag_create, eopnotsupp);
__weak_alias(pci_chipset_tag_destroy, voidop);
__weak_alias(bus_space_reserve, eopnotsupp);
__weak_alias(bus_space_reserve_subregion, eopnotsupp);
__weak_alias(bus_space_release, voidop);
__weak_alias(bus_space_reservation_map, eopnotsupp);
__weak_alias(bus_space_reservation_unmap, voidop);
__weak_alias(bus_dma_tag_create, eopnotsupp);
__weak_alias(bus_dma_tag_destroy, voidop);
__weak_alias(bus_space_tag_create, eopnotsupp);
__weak_alias(bus_space_tag_destroy, voidop);
__strict_weak_alias(bus_space_is_equal, default_bus_space_is_equal);
__strict_weak_alias(bus_space_handle_is_equal,
    default_bus_space_handle_is_equal);
__weak_alias(userconf_bootinfo, voidop);
__weak_alias(userconf_init, voidop);
__weak_alias(userconf_prompt, voidop);

__weak_alias(kobj_renamespace, nullop);

__weak_alias(interrupt_get_count, nullop);
__weak_alias(interrupt_get_assigned, voidop);
__weak_alias(interrupt_get_available, voidop);
__weak_alias(interrupt_get_devname, voidop);
__weak_alias(interrupt_construct_intrids, nullret);
__weak_alias(interrupt_destruct_intrids, voidop);
__weak_alias(interrupt_distribute, eopnotsupp);
__weak_alias(interrupt_distribute_handler, eopnotsupp);

/*
 * Scheduler activations system calls.  These need to remain until libc's
 * major version is bumped.
 */
__strong_alias(sys_sa_register,sys_nosys);
__strong_alias(sys_sa_stacks,sys_nosys);
__strong_alias(sys_sa_enable,sys_nosys);
__strong_alias(sys_sa_setconcurrency,sys_nosys);
__strong_alias(sys_sa_yield,sys_nosys);
__strong_alias(sys_sa_preempt,sys_nosys);
__strong_alias(sys_sa_unblockyield,sys_nosys);

/*
 * Stubs for compat_netbsd32.
 */
__strong_alias(dosa_register,sys_nosys);
__strong_alias(sa_stacks1,sys_nosys);

/*
 * Stubs for drivers.  See sys/conf.h.
 */
__strong_alias(devenodev,enodev);
__strong_alias(deveopnotsupp,eopnotsupp);
__strong_alias(devnullop,nullop);
__strong_alias(ttyenodev,enodev);
__strong_alias(ttyvenodev,voidop);
__strong_alias(ttyvnullop,nullop);
#else

void device_register_noop(device_t dev, void *aux)
{

}

void device_register_post_config_noop(device_t dev, void *aux)
{

}

struct intrids_handler *
interrupt_construct_intrids_noop(const kcpuset_t *cpuset)
{
	return NULL;
}

void
interrupt_destruct_intrids_noop(struct intrids_handler *iih)
{

}

uint64_t
interrupt_get_count_noop(const char *intrid, u_int cpu_idx)
{
	return 0;
}

void
interrupt_get_devname_noop(const char *intrid, char *buf, size_t len)
{

}

void
interrupt_get_assigned_noop(const char *intrid, kcpuset_t *cpuset)
{

}

int
interrupt_distribute_handler_noop(const char *intrid, const kcpuset_t *newset, kcpuset_t *oldset)
{
	return (EOPNOTSUPP);
}

void
machdep_init_noop(void)
{

}

void
interrupt_get_available_noop(kcpuset_t *cpuset)
{

}

// TODO: wasm32 Might need dedicated null-ops for these.
void device_register(device_t, void *) __attribute__((weak, alias("device_register_noop")));
void device_register_post_config(device_t, void *) __attribute__((weak, alias("device_register_post_config_noop")));
void spldebug_start(void) __attribute__((weak, alias("voidop")));
void spldebug_stop(void) __attribute__((weak, alias("voidop")));
void machdep_init(void) __attribute__((weak, alias("machdep_init_noop")));
void pci_chipset_tag_create(void) __attribute__((weak, alias("eopnotsupp")));
void pci_chipset_tag_destroy(void) __attribute__((weak, alias("voidop")));
int	bus_space_reserve(bus_space_tag_t, bus_addr_t, bus_size_t, int, bus_space_reservation_t *) __attribute__((weak, alias("eopnotsupp")));
int bus_space_reserve_subregion(bus_space_tag_t,
    bus_addr_t, bus_addr_t, bus_size_t, bus_size_t, bus_size_t,
    int, bus_space_reservation_t *) __attribute__((weak, alias("eopnotsupp")));
void bus_space_release(bus_space_tag_t, bus_space_reservation_t *) __attribute__((weak, alias("voidop")));
int bus_space_reservation_map(bus_space_tag_t, bus_space_reservation_t *,
    int, bus_space_handle_t *) __attribute__((weak, alias("eopnotsupp")));
void bus_space_reservation_unmap(bus_space_tag_t, bus_space_handle_t,
    bus_size_t) __attribute__((weak, alias("voidop")));
int bus_dma_tag_create(bus_dma_tag_t, uint64_t,
    const struct bus_dma_overrides *, void *, bus_dma_tag_t *) __attribute__((weak, alias("eopnotsupp")));
void bus_dma_tag_destroy(bus_dma_tag_t) __attribute__((weak, alias("voidop")));
int	bus_space_tag_create(bus_space_tag_t, uint64_t, uint64_t,
	                     const struct bus_space_overrides *, void *,
	                     bus_space_tag_t *) __attribute__((weak, alias("eopnotsupp")));
void bus_space_tag_destroy(bus_space_tag_t) __attribute__((weak, alias("voidop")));
bool bus_space_is_equal(bus_space_tag_t, bus_space_tag_t) __attribute__((weak, alias("default_bus_space_is_equal")));
bool bus_space_handle_is_equal(bus_space_tag_t, bus_space_handle_t,
    bus_space_handle_t) __attribute__((weak, alias("default_bus_space_handle_is_equal")));
void userconf_bootinfo(void) __attribute__((weak, alias("voidop")));
void userconf_init(void) __attribute__((weak, alias("voidop")));
void userconf_prompt(void) __attribute__((weak, alias("voidop")));

void kobj_renamespace(void) __attribute__((weak, alias("nullop")));

uint64_t interrupt_get_count(const char *, u_int) __attribute__((weak, alias("interrupt_get_count_noop")));
void interrupt_get_assigned(const char *, kcpuset_t *) __attribute__((weak, alias("interrupt_get_assigned_noop")));
void interrupt_get_available(kcpuset_t *) __attribute__((weak, alias("interrupt_get_available_noop")));
void interrupt_get_devname(const char *, char *, size_t) __attribute__((weak, alias("interrupt_get_devname_noop")));
void interrupt_construct_intrids(void) __attribute__((weak, alias("interrupt_construct_intrids_noop")));
void interrupt_destruct_intrids(struct intrids_handler *) __attribute__((weak, alias("interrupt_destruct_intrids_noop")));
void interrupt_distribute(void) __attribute__((weak, alias("eopnotsupp")));
int interrupt_distribute_handler(const char *, const kcpuset_t *, kcpuset_t *) __attribute__((weak, alias("interrupt_distribute_handler_noop")));

// Scheduler activations system calls.  These need to remain until libc's major version is bumped.
void sys_sa_register(void) __attribute__((alias("sys_nosys")));
void sys_sa_stacks(void) __attribute__((alias("sys_nosys")));
void sys_sa_enable(void) __attribute__((alias("sys_nosys")));
void sys_sa_setconcurrency(void) __attribute__((alias("sys_nosys")));
void sys_sa_yield(void) __attribute__((alias("sys_nosys")));
void sys_sa_preempt(void) __attribute__((alias("sys_nosys")));
void sys_sa_unblockyield(void) __attribute__((alias("sys_nosys")));

// Stubs for compat_netbsd32.
void dosa_register(void) __attribute__((alias("sys_nosys")));
void sa_stacks1(void) __attribute__((alias("sys_nosys")));

// Stubs for drivers.  See sys/conf.h.
void devenodev(void) __attribute__((alias("enodev")));
void deveopnotsupp(void) __attribute__((alias("eopnotsupp")));
void devnullop(void) __attribute__((alias("nullop")));
void ttyenodev(void) __attribute__((alias("enodev")));
void ttyvenodev(void) __attribute__((alias("voidop")));
void ttyvnullop(void) __attribute__((alias("nullop")));
#endif
/*
 * Stubs for architectures that do not support kernel preemption.
 */
#ifndef __HAVE_PREEMPTION
bool
cpu_kpreempt_enter(uintptr_t where, int s)
{

	return false;
}

void
cpu_kpreempt_exit(uintptr_t where)
{

}

bool
cpu_kpreempt_disabled(void)
{

	return true;
}
#else
# ifndef MULTIPROCESSOR
#   error __HAVE_PREEMPTION requires MULTIPROCESSOR
# endif
#endif	/* !__HAVE_PREEMPTION */

int
sys_nosys(struct lwp *l, const void *v, register_t *retval)
{

	mutex_enter(&proc_lock);
	psignal(l->l_proc, SIGSYS);
	mutex_exit(&proc_lock);
	return ENOSYS;
}

/*
 * Unsupported device function (e.g. writing to read-only device).
 */
int
enodev(void)
{

	return (ENODEV);
}

/*
 * Unconfigured device function; driver not configured.
 */
int
enxio(void)
{

	return (ENXIO);
}

/*
 * Unsupported ioctl function.
 */
int
enoioctl(void)
{

	return (ENOTTY);
}

/*
 * Unsupported system function.
 * This is used for an otherwise-reasonable operation
 * that is not supported by the current system binary.
 */
int
enosys(void)
{

	return (ENOSYS);
}

/*
 * Return error for operation not supported
 * on a specific object or file type.
 */
int
eopnotsupp(void)
{

	return (EOPNOTSUPP);
}

/*
 * Generic null operation, void return value.
 */
void
voidop(void)
{
}

/*
 * Generic null operation, always returns success.
 */
int
nullop(void *v)
{

	return (0);
}

/*
 * Generic null operation, always returns null.
 */
void *
nullret(void)
{

	return (NULL);
}

bool
default_bus_space_handle_is_equal(bus_space_tag_t t,
    bus_space_handle_t h1, bus_space_handle_t h2)
{

	return memcmp(&h1, &h2, sizeof(h1)) == 0;
}

bool
default_bus_space_is_equal(bus_space_tag_t t1, bus_space_tag_t t2)
{

	return memcmp(&t1, &t2, sizeof(t1)) == 0;
}

/* Stubs for architectures with no kernel FPU access.  */
#ifndef __WASM
__weak_alias(kthread_fpu_enter_md, voidop);
__weak_alias(kthread_fpu_exit_md, voidop);
#else
void kthread_fpu_enter_md(void) __attribute__((weak, alias("voidop")));
void kthread_fpu_exit_md(void) __attribute__((weak, alias("voidop")));
#endif
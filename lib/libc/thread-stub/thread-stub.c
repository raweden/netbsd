/*	$NetBSD: thread-stub.c,v 1.32 2022/04/19 20:32:16 rillig Exp $	*/

/*-
 * Copyright (c) 2003, 2009 The NetBSD Foundation, Inc.
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

#include <sys/cdefs.h>
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: thread-stub.c,v 1.32 2022/04/19 20:32:16 rillig Exp $");
#endif /* LIBC_SCCS and not lint */

/*
 * Stubs for thread operations, for use when threads are not used by
 * the application.  See "reentrant.h" for details.
 */

#ifdef _REENTRANT

#define	__LIBC_THREAD_STUBS

#include "namespace.h"
#include "reentrant.h"
#include "tsd.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

extern int __isthreaded;

#define	DIE()	(void)raise(SIGABRT)

#define	CHECK_NOT_THREADED_ALWAYS()	\
do {					\
	if (__isthreaded)		\
		DIE();			\
} while (0)

#if 1
#define	CHECK_NOT_THREADED()	CHECK_NOT_THREADED_ALWAYS()
#else
#define	CHECK_NOT_THREADED()	/* nothing */
#endif

int __libc_pthread_join(pthread_t thread, void **valptr) __attribute__((weak, alias("pthread_join")));
int __libc_pthread_detach(pthread_t thread) __attribute__((weak, alias("pthread_detach")));

int
pthread_join(pthread_t thread, void **valptr)
{

	if (thread == pthread_self())
		return EDEADLK;
	return ESRCH;
}

int
pthread_detach(pthread_t thread)
{

	if (thread == pthread_self())
		return 0;
	return ESRCH;
}

/* mutexes */

int __libc_mutex_catchall_stub(mutex_t *);

int __libc_mutex_init(mutex_t *m, const mutexattr_t *a) __attribute__((weak, alias("__libc_mutex_init_stub")));
int __libc_mutex_lock(mutex_t *) __attribute__((weak, alias("__libc_mutex_catchall_stub")));
int __libc_mutex_trylock(mutex_t *) __attribute__((weak, alias("__libc_mutex_catchall_stub")));
int __libc_mutex_unlock(mutex_t *) __attribute__((weak, alias("__libc_mutex_catchall_stub")));
int __libc_mutex_destroy(mutex_t *) __attribute__((weak, alias("__libc_mutex_catchall_stub")));

int __libc_mutex_lock_stub(mutex_t *) __attribute__((alias("__libc_mutex_catchall_stub")));
int __libc_mutex_trylock_stub(mutex_t *) __attribute__((alias("__libc_mutex_catchall_stub")));
int __libc_mutex_unlock_stub(mutex_t *) __attribute__((alias("__libc_mutex_catchall_stub")));
int __libc_mutex_destroy_stub(mutex_t *) __attribute__((alias("__libc_mutex_catchall_stub")));

int __libc_mutexattr_catchall_stub(mutexattr_t *);

int __libc_mutexattr_init(mutexattr_t *) __attribute__((weak, alias("__libc_mutexattr_catchall_stub")));
int __libc_mutexattr_destroy(mutexattr_t *) __attribute__((weak, alias("__libc_mutexattr_catchall_stub")));
int __libc_mutexattr_settype(mutexattr_t *ma, int type) __attribute__((weak, alias("__libc_mutexattr_settype_stub")));

int __libc_mutexattr_init_stub(mutexattr_t *) __attribute__((alias("__libc_mutexattr_catchall_stub")));
int __libc_mutexattr_destroy_stub(mutexattr_t *) __attribute__((alias("__libc_mutexattr_catchall_stub")));

int
__libc_mutex_init_stub(mutex_t *m, const mutexattr_t *a)
{
	/* LINTED deliberate lack of effect */
	(void)m;
	/* LINTED deliberate lack of effect */
	(void)a;

	CHECK_NOT_THREADED();

	return (0);
}

int
__libc_mutex_catchall_stub(mutex_t *m)
{
	/* LINTED deliberate lack of effect */
	(void)m;

	CHECK_NOT_THREADED();

	return (0);
}

int
__libc_mutexattr_settype_stub(mutexattr_t *ma, int type)
{
	/* LINTED deliberate lack of effect */
	(void)ma;
	/* LINTED deliberate lack of effect */
	(void)type;

	CHECK_NOT_THREADED();

	return (0);
}

int
__libc_mutexattr_catchall_stub(mutexattr_t *ma)
{
	/* LINTED deliberate lack of effect */
	(void)ma;

	CHECK_NOT_THREADED();

	return (0);
}

/* condition variables */

int __libc_cond_catchall_stub(cond_t *);

int __libc_cond_init(cond_t *c, const condattr_t *ad) __attribute__((weak, alias("__libc_cond_init_stub")));
int __libc_cond_signal(cond_t *) __attribute__((weak, alias("__libc_cond_catchall_stub")));
int __libc_cond_broadcast(cond_t *d) __attribute__((weak, alias("__libc_cond_catchall_stub")));
//int __libc_cond_wait(cond_t *, mutex_t *) __attribute__((weak, alias("__libc_cond_catchall_stub")));
int __libc_cond_timedwait(cond_t *c, mutex_t *m, const struct timespec *t) __attribute__((weak, alias("__libc_cond_timedwait_stub")));
int __libc_cond_destroy(cond_t *) __attribute__((weak, alias("__libc_cond_catchall_stub")));

int __libc_cond_signal_stub(cond_t *) __attribute__((alias("__libc_cond_catchall_stub")));
int __libc_cond_broadcast_stub(cond_t *) __attribute__((alias("__libc_cond_catchall_stub")));
int __libc_cond_destroy_stub(cond_t *) __attribute__((alias("__libc_cond_catchall_stub")));

int
__libc_cond_init_stub(cond_t *c, const condattr_t *a)
{
	/* LINTED deliberate lack of effect */
	(void)c;
	/* LINTED deliberate lack of effect */
	(void)a;

	CHECK_NOT_THREADED();

	return (0);
}

int
__libc_cond_wait_stub(cond_t *c, mutex_t *m)
{
	/* LINTED deliberate lack of effect */
	(void)c;
	/* LINTED deliberate lack of effect */
	(void)m;

	CHECK_NOT_THREADED();

	return (0);
}

int
__libc_cond_wait(cond_t *c, mutex_t *m)
{
	/* LINTED deliberate lack of effect */
	(void)c;
	/* LINTED deliberate lack of effect */
	(void)m;

	CHECK_NOT_THREADED();

	return (0);
}

int
__libc_cond_timedwait_stub(cond_t *c, mutex_t *m, const struct timespec *t)
{
	/* LINTED deliberate lack of effect */
	(void)c;
	/* LINTED deliberate lack of effect */
	(void)m;
	/* LINTED deliberate lack of effect */
	(void)t;

	CHECK_NOT_THREADED();

	return (0);
}

int
__libc_cond_catchall_stub(cond_t *c)
{
	/* LINTED deliberate lack of effect */
	(void)c;

	CHECK_NOT_THREADED();

	return (0);
}


/* read-write locks */

int __libc_rwlock_catchall_stub(rwlock_t *);

int __libc_rwlock_init(rwlock_t *, const rwlockattr_t *) __attribute__((weak, alias("__libc_rwlock_init_stub")));
int __libc_rwlock_rdlock(rwlock_t *) __attribute__((weak, alias("__libc_rwlock_catchall_stub")));
int __libc_rwlock_wrlock(rwlock_t *) __attribute__((weak, alias("__libc_rwlock_catchall_stub")));
int __libc_rwlock_tryrdlock(rwlock_t *) __attribute__((weak, alias("__libc_rwlock_catchall_stub")));
int __libc_rwlock_trywrlock(rwlock_t *) __attribute__((weak, alias("__libc_rwlock_catchall_stub")));
int __libc_rwlock_unlock(rwlock_t *) __attribute__((weak, alias("__libc_rwlock_catchall_stub")));
int __libc_rwlock_destroy(rwlock_t *) __attribute__((weak, alias("__libc_rwlock_catchall_stub")));

int __libc_rwlock_rdlock_stub(rwlock_t *) __attribute__((alias("__libc_rwlock_catchall_stub")));
int __libc_rwlock_wrlock_stub(rwlock_t *) __attribute__((alias("__libc_rwlock_catchall_stub")));
int __libc_rwlock_tryrdlock_stub(rwlock_t *) __attribute__((alias("__libc_rwlock_catchall_stub")));
int __libc_rwlock_trywrlock_stub(rwlock_t *) __attribute__((alias("__libc_rwlock_catchall_stub")));
int __libc_rwlock_unlock_stub(rwlock_t *) __attribute__((alias("__libc_rwlock_catchall_stub")));
int __libc_rwlock_destroy_stub(rwlock_t *) __attribute__((alias("__libc_rwlock_catchall_stub")));

int
__libc_rwlock_init_stub(rwlock_t *l, const rwlockattr_t *a)
{
	/* LINTED deliberate lack of effect */
	(void)l;
	/* LINTED deliberate lack of effect */
	(void)a;

	CHECK_NOT_THREADED();

	return (0);
}

int
__libc_rwlock_catchall_stub(rwlock_t *l)
{
	/* LINTED deliberate lack of effect */
	(void)l;

	CHECK_NOT_THREADED();

	return (0);
}


/*
 * thread-specific data; we need to actually provide a simple TSD
 * implementation, since some thread-safe libraries want to use it.
 */

struct __libc_tsd __libc_tsd[TSD_KEYS_MAX];
static int __libc_tsd_nextkey;

int __libc_thr_keycreate(thread_key_t *k, void (*d)(void *)) __attribute__((weak, alias("__libc_thr_keycreate_stub")));
int __libc_thr_setspecific(thread_key_t k, const void *v) __attribute__((weak, alias("__libc_thr_setspecific_stub")));
void *__libc_thr_getspecific(thread_key_t k) __attribute__((weak, alias("__libc_thr_getspecific_stub")));
int __libc_thr_keydelete(thread_key_t k) __attribute__((weak, alias("__libc_thr_keydelete_stub")));

int
__libc_thr_keycreate_stub(thread_key_t *k, void (*d)(void *))
{
	int i;

	for (i = __libc_tsd_nextkey; i < TSD_KEYS_MAX; i++) {
		if (__libc_tsd[i].tsd_inuse == 0)
			goto out;
	}

	for (i = 0; i < __libc_tsd_nextkey; i++) {
		if (__libc_tsd[i].tsd_inuse == 0)
			goto out;
	}

	return (EAGAIN);

 out:
	/*
	 * XXX We don't actually do anything with the destructor.  We
	 * XXX probably should.
	 */
	__libc_tsd[i].tsd_inuse = 1;
	__libc_tsd_nextkey = (i + i) % TSD_KEYS_MAX;
	__libc_tsd[i].tsd_dtor = d;
	*k = i;

	return (0);
}

int
__libc_thr_setspecific_stub(thread_key_t k, const void *v)
{

	__libc_tsd[k].tsd_val = __UNCONST(v);

	return (0);
}

void *
__libc_thr_getspecific_stub(thread_key_t k)
{

	return (__libc_tsd[k].tsd_val);
}

int
__libc_thr_keydelete_stub(thread_key_t k)
{

	/*
	 * XXX Do not recycle key; see big comment in libpthread.
	 */

	__libc_tsd[k].tsd_dtor = NULL;

	return (0);
}


/* misc. */

int __libc_thr_once(once_t *o, void (*r)(void))__attribute__((weak, alias("__libc_thr_once_stub")));
int __libc_thr_sigsetmask(int h, const sigset_t *s, sigset_t *o) __attribute__((weak, alias("__libc_thr_sigsetmask_stub")));
thr_t __libc_thr_self(void) __attribute__((weak, alias("__libc_thr_self_stub")));
int __libc_thr_yield(void) __attribute__((weak, alias("__libc_thr_yield_stub")));
int __libc_thr_create(thr_t *tp, const thrattr_t *ta,
    void *(*f)(void *), void *a) __attribute__((weak, alias("__libc_thr_create_stub")));
__dead void __libc_thr_exit(void *v) __attribute__((weak, alias("__libc_thr_exit_stub")));
int __libc_thr_setcancelstate(int new, int *old) __attribute__((weak, alias("__libc_thr_setcancelstate_stub")));
int __libc_thr_equal(pthread_t t1, pthread_t t2) __attribute__((weak, alias("__libc_thr_equal_stub")));
unsigned int __libc_thr_curcpu(void) __attribute__((weak, alias("__libc_thr_curcpu_stub")));

int
__libc_thr_once_stub(once_t *o, void (*r)(void))
{

	/* XXX Knowledge of libpthread types. */

	if (o->pto_done == 0) {
		(*r)();
		o->pto_done = 1;
	}

	return (0);
}

int
__libc_thr_sigsetmask_stub(int h, const sigset_t *s, sigset_t *o)
{

	CHECK_NOT_THREADED();

	if (sigprocmask(h, s, o))
		return errno;
	return 0;
}

thr_t
__libc_thr_self_stub(void)
{

	return ((thr_t) -1);
}

/* This is the strong symbol generated for sched_yield(2) via WSYSCALL() */
int __sys_sched_yield(void);

int
__libc_thr_yield_stub(void)
{
	return __sys_sched_yield();
}

int
__libc_thr_create_stub(thr_t *tp, const thrattr_t *ta,
    void *(*f)(void *), void *a)
{
	/* LINTED deliberate lack of effect */
	(void)tp;
	/* LINTED deliberate lack of effect */
	(void)ta;
	/* LINTED deliberate lack of effect */
	(void)f;
	/* LINTED deliberate lack of effect */
	(void)a;

	DIE();

	return (EOPNOTSUPP);
}

__dead void
__libc_thr_exit_stub(void *v)
{
	/* LINTED deliberate lack of effect */
	(void)v;
	exit(0);
}

int
__libc_thr_setcancelstate_stub(int new, int *old)
{
	/* LINTED deliberate lack of effect */
	(void)new;

	/* LINTED deliberate lack of effect */
	(void)old;

	CHECK_NOT_THREADED();

	return (0);
}

int
__libc_thr_equal_stub(pthread_t t1, pthread_t t2)
{

	/* assert that t1=t2=pthread_self() */
	return (t1 == t2);
}

unsigned int
__libc_thr_curcpu_stub(void)
{

	return (0);
}

#endif /* _REENTRANT */

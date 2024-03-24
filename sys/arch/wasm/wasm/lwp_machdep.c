/*
 * Copyright (c) 2024 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Raweden @github 2024.
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
 */

#include <sys/null.h>
#include <sys/param.h>
#include <sys/lwp.h>

#include <wasm/frame.h>
#include <wasm/wasm-extra.h>


volatile struct lwp *wasm_curlwp;


// THIS DID NOT WORK-OUT since its fools clang into taking skip most of the optimization of
// having a point such as simply put it within in a static list of arguments.
/**
 * this is replaced to a wasm instruction in post-edit.
 * @return Returns the current lwp.
 */
__noinline
struct lwp *
x86_curlwp(void) 
{
	__panic_abort();
	return wasm_curlwp;
}

#define STD_WAKEUP 1234
#define SIG_WAKEUP 5678

int __noinline
wasm_lwp_wait(lwp_t *l, int64_t timo)
{
	int result;
	uint32_t sig;
	
	if ((l->l_pflag & LP_RUNNING) != 0) {
		l->l_pflag &= ~LP_RUNNING;
	}
	
	while (true) {

		result = atomic_wait32((uint32_t *)&l->l_md.md_wakesig, 0, timo);
		if (result == ATOMIC_WAIT_NOT_EQUAL) {
			printf("expected is non zero");
			__panic_abort();
		}
		
		sig = atomic_load32((uint32_t *)&l->l_md.md_wakesig);
		if (sig == STD_WAKEUP) {
			atomic_cmpxchg32((uint32_t *)&l->l_md.md_wakesig, sig, 0);
			break;
		} else if (sig == SIG_WAKEUP) {
			printf("lwp awake with signal = %d", sig);
			// go back to sleep
		} else {
			printf("lwp awake with signal = %d", sig);
			break;
		}
	}

	// mark ourself as running.
	if ((l->l_pflag & LP_RUNNING) == 0) {
		l->l_pflag |= LP_RUNNING;
	}

	return 0;
}

int __noinline
wasm_lwp_awake(lwp_t *l)
{
	int result;

	atomic_xchg32((uint32_t *)&l->l_md.md_wakesig, STD_WAKEUP);
	result = atomic_notify((uint32_t *)&l->l_md.md_wakesig, 1);

	return 0;
}

int __noinline
wasm_lwp_awake_signal(lwp_t *l)
{
	int result;

	atomic_xchg32((uint32_t *)&l->l_md.md_wakesig, SIG_WAKEUP);
	result = atomic_notify((uint32_t *)&l->l_md.md_wakesig, 1);

	return 0;
}

/**
 * The purpose of the trampoline is to enter into the threads, some threads
 * is spawned to provide a user-space program, these are handled trough a function 
 * called by this trampoline which setups additional runtime and later enters into `main()`
 *
 * Where the trampoline jumps to is directed by the bottom switchframe, which holds a 
 * callback along with one argument given to that callback.
 */
void
lwp_trampoline(void)
{
	struct lwp *l;
	struct pcb *pcb;
	struct switchframe *sf;
	void *arg;
	void (*func)(void *);
	
	// FIXME: test if pool allocation bug is by the CPU
	l->l_cpu = lwp0.l_cpu;

	l = (struct lwp *)curlwp;
	pcb = lwp_getpcb(l);

	if ((struct lwp *)(pcb->pcb_ebp) != l) {
		printf("PANIC: pcb->pcb_ebp (%p) != curlwp (%p) pcb = %p\n", (void *)pcb->pcb_ebp, l, pcb);
		__panic_abort();
	}

	sf = (struct switchframe *)pcb->pcb_esp;
	printf("%s reading switchframe at %p for lwp %p\n", __func__, sf, l);
#if 0
	if (sf->sf_eip != (int)lwp_trampoline) {
		printf("PANIC: pcb->sf_eip != lwp_trampoline");
		__panic_abort();
	}
#endif

	func = (void (*)(void *))sf->sf_esi;
	arg = (void *)sf->sf_ebx;

	if (func == NULL) {
		printf("PANIC: lwp @%p sf->sf_esi == NULL (stackframe %p)", l, sf);
		__panic_abort();
	}

	func(arg);
	// for the must part we never return from here.. since sys_exit kills the rewind of the stack.

    // usually declared in locore.S
#if 0
ENTRY(lwp_trampoline)
	movq	%rbp,%rsi
	movq	%rbp,%r14	/* for .Lsyscall_checkast */
	movq	%rax,%rdi
	xorq	%rbp,%rbp
	KMSAN_INIT_ARG(16)
	call	_C_LABEL(lwp_startup)
	movq	%r13,%rdi
	KMSAN_INIT_ARG(8)
	call	*%r12
	jmp	.Lsyscall_checkast
END(lwp_trampoline)
#endif
}

int lwp_create_jsworker(struct lwp *l1, pid_t *pid_res, bool *child_ok, struct lwp **child_lwp, struct runtime_abi *abi);

static struct runtime_abi __abi_display_server_wrapper = {
	.ra_path_type = RUNTIME_ABI_PATH_URL,
	.ra_abi_pathlen = (sizeof("display-server.js") - 1),
	.abi_path = "display-server.js"
};

void
init_display_server(void)
{
	struct proc *p2;
	struct lwp *l2;
	bool child_ok;
	pid_t child_pid;
	int error;

	l2 = NULL;
	child_ok = false;

	error = lwp_create_jsworker(&lwp0, &child_pid, &child_ok, &l2, &__abi_display_server_wrapper);

	if (error != 0 || l2 == NULL) {
		printf("%s error = %d while spawning js-worker\n", __func__, error);
		return;
	}
}
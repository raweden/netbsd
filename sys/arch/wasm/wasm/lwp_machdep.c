

#include "arch/wasm/include/cpu.h"
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

int __noinline wasm_lwp_wait(lwp_t *l, int64_t timo)
{
	int result;
	uint32_t sig;
	
	if ((l->l_pflag & LP_RUNNING) != 0) {
		l->l_pflag &= ~LP_RUNNING;
	}
	
	result = atomic_wait32((uint32_t *)&l->l_wa_wakesig, 0, timo);
	if (result == ATOMIC_WAIT_NOT_EQUAL) {
		printf("expected is non zero");
		__panic_abort();
	}
	
	sig = atomic_load32((uint32_t *)&l->l_wa_wakesig);
	if (sig == 1234) {
		atomic_cmpxchg32((uint32_t *)&l->l_wa_wakesig, sig, 0);
	} else {
		printf("lwp awake with signal = %d", sig);
	}

	// mark ourself as running.
	if ((l->l_pflag & LP_RUNNING) == 0) {
		l->l_pflag |= LP_RUNNING;
	}

	return 0;
}

int __noinline wasm_lwp_awake(lwp_t *l)
{
	int result;

	atomic_xchg32((uint32_t *)&l->l_wa_wakesig, 1234);
	result = atomic_notify((uint32_t *)&l->l_wa_wakesig, 1);


	return 0;
}

/*
 * void lwp_trampoline(void);
 *
 * This is a trampoline function pushed run by newly created LWPs
 * in order to do additional setup in their context.
 */
void lwp_trampoline(void)
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
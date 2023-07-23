
#include <sys/lwp.h>


struct lwp *wasm_curlwp;



/*
 * void lwp_trampoline(void);
 *
 * This is a trampoline function pushed run by newly created LWPs
 * in order to do additional setup in their context.
 */
void lwp_trampoline(void)
{
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
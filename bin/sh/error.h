/*	$NetBSD: error.h,v 1.25 2023/03/21 08:31:30 hannken Exp $	*/

/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
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
 *	@(#)error.h	8.2 (Berkeley) 5/4/95
 */

#include <stdarg.h>

/*
 * Types of operations (passed to the errmsg routine).
 */

#define E_OPEN		0x1	/* opening a file */
#define E_CREAT		0x2	/* creating a file */
#define E_EXEC		0x4	/* executing a program */


/*
 * We enclose jmp_buf in a structure so that we can declare pointers to
 * jump locations.  The global variable handler contains the location to
 * jump to when an exception occurs, and the global variable exception
 * contains a code identifying the exeception.  To implement nested
 * exception handlers, the user should save the value of handler on entry
 * to an inner scope, set handler to point to a jmploc structure for the
 * inner scope, and restore handler on exit from the scope.
 */

#include <setjmp.h>

struct jmploc {
	sigjmp_buf loc;
};

extern volatile int errors_suppressed;
extern const char * volatile currentcontext;

extern struct jmploc *handler;
extern int exception;
extern int exerrno;	/* error for EXEXEC */

/* exceptions */
#define EXINT 0		/* SIGINT received */
#define EXERROR 1	/* a generic error */
#define EXSHELLPROC 2	/* execute a shell procedure */
#define EXEXEC 3	/* command execution failed */
#define EXEXIT 4	/* shell wants to exit(exitstatus) */


/*
 * These macros allow the user to suspend the handling of interrupt signals
 * over a period of time.  This is similar to SIGHOLD to or sigblock, but
 * much more efficient and portable.  (But hacking the kernel is so much
 * more fun than worrying about efficiency and portability. :-))
 */

extern volatile int suppressint;
extern volatile int intpending;

#define INTOFF suppressint++
#define INTON do { if (--suppressint == 0 && intpending) onint(); } while (0)
#define FORCEINTON do { suppressint = 0; if (intpending) onint(); } while (0)
#define CLEAR_PENDING_INT (intpending = 0)
#define int_pending() intpending

#if ! defined(SHELL_BUILTIN)
void exraise(int) __dead;
void onint(void);
void error(const char *, ...) __dead __printflike(1, 2);
void exerror(int, const char *, ...) __dead __printflike(2, 3);
const char *errmsg(int, int);
#else
void error(const char *msg, ...) __printflike(1, 2);
void exerror(int cond, const char *msg, ...) __printflike(2, 3);
#endif /* ! SHELL_BUILTIN */

void sh_err(int, const char *, ...) __dead __printflike(2, 3);
void sh_verr(int, const char *, va_list) __dead __printflike(2, 0);
void sh_errx(int, const char *, ...) __dead __printflike(2, 3);
void sh_verrx(int, const char *, va_list) __dead __printflike(2, 0);
void sh_warn(const char *, ...) __printflike(1, 2);
void sh_vwarn(const char *, va_list) __printflike(1, 0);
void sh_warnx(const char *, ...) __printflike(1, 2);
void sh_vwarnx(const char *, va_list) __printflike(1, 0);

void sh_exit(int) __dead;

#ifdef __WASM
#ifndef __WASM_IMPORT
#define __WASM_IMPORT(module, symbol) __attribute__((import_module(#module), import_name(#symbol)))
#endif
#include <setjmp.h>
typedef struct label_t {
	int val[6];
} label_t;
int sigsetjmp(sigjmp_buf, int) __WASM_IMPORT(sys, setjmp) __returns_twice;
void siglongjmp(sigjmp_buf, int) __WASM_IMPORT(sys, longjmp) __dead;
#endif

/*
 * BSD setjmp saves the signal mask, which violates ANSI C and takes time,
 * so we use sigsetjmp instead, and explicitly do not save it.
 * sh does a lot of setjmp() calls (fewer longjmp though).
 */

#define setjmp(jmploc)		sigsetjmp((jmploc), 0)
#define longjmp(jmploc, val)	siglongjmp((jmploc), (val))

/* $NetBSD: crt0.c,v 1.27 2022/06/21 06:52:17 skrll Exp $ */

/*
 * 
 * Based on lib/csu/common/crt0-common.c by
 * Christos Zoulas & Christopher G. Demetriou (c)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *          This product includes software developed for the
 *          NetBSD Project.  See http://www.NetBSD.org/ for
 *          information about NetBSD.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * <<Id: LICENSE,v 1.2 2000/06/14 15:57:33 cgd Exp>>
 */

#include <stdint.h>
#include <sys/cdefs.h>
__RCSID("$NetBSD: crt0-common.c,v 1.27 2022/06/21 06:52:17 skrll Exp $");


#include <sys/types.h>
#include <sys/exec.h>
#include <sys/syscall.h>
#include <machine/profile.h>
#include <stdlib.h>
#include <unistd.h>

extern void _exit (int __status);

#define	_FATAL(str)				        \
do {						            \
	write(2, str, sizeof(str)-1);		\
	_exit(1);				            \
} while (0)

extern struct ps_strings *__ps_strings;
extern char *__progname;
extern char **environ;

void _libc_init(void);

#define DYNLD_IOCTL_RUN_PRE_INIT 12
#define DYNLD_IOCTL_RUN_INIT_ARRAY 13
#define DYNLD_IOCTL_SETUP_FNIT_ARRAY 14

#ifndef __WASM_IMPORT
#define __WASM_IMPORT(module, symbol) __attribute__((import_module(#module), import_name(#symbol)))
#endif

int __dynld_ioctl(int cmd, void *data) __WASM_IMPORT(dlfcn, __ld_ioctl);
int main_entrypoint(uint32_t argc, char	**argv, char **envp) __WASM_IMPORT(dlfcn, main_entrypoint);

void
run_preinit(void)
{
    __dynld_ioctl(DYNLD_IOCTL_RUN_PRE_INIT, NULL);
}

void
run_initarray(void)
{
    __dynld_ioctl(DYNLD_IOCTL_RUN_INIT_ARRAY, NULL);
}

void
setup_finiarray(void)
{
    __dynld_ioctl(DYNLD_IOCTL_SETUP_FNIT_ARRAY, NULL);
}

/* 
 * invoked from shared loader 
 */
void
__start(void (*cleanup)(void), struct ps_strings *ps_strings)
{
	if (ps_strings == NULL) {
		_FATAL("ps_strings missing\n");
    }
	__ps_strings = ps_strings;

	environ = ps_strings->ps_envstr;

	if (ps_strings->ps_argvstr[0] != NULL) {
		char *c;
		__progname = ps_strings->ps_argvstr[0];
		for (c = ps_strings->ps_argvstr[0]; *c; ++c) {
			if (*c == '/')
				__progname = c + 1;
		}
	} else {
		__progname = "";
	}

	if (cleanup != NULL)
		atexit(cleanup);

	_libc_init();

    run_preinit();
    
    setup_finiarray();
	
    run_initarray();

	exit(main_entrypoint(ps_strings->ps_nargvstr, ps_strings->ps_argvstr, environ));
}
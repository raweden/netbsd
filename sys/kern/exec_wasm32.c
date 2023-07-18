/*	$NetBSD: exec_wasm32.c,v 1.143 2023/07/09 10:13:53 pgoyette Exp $	*/

/*
 * Copyright (c) 2023 Jesper Svensson (raweden)
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by 2023 Jesper Svensson
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 */

#include "sigtypes.h"
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: exec_elf32.c,v 1.143 2019/11/20 19:37:53 pgoyette Exp $");


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/exec.h>
#include <sys/exec_wasm.h>
#include <sys/resourcevar.h>
#include <sys/module.h>

static int exec_wasm32_modcmd(modcmd_t, void *);

MODULE(MODULE_CLASS_EXEC, exec_wasm32, NULL);

static int exec_wasm32_makecmds(struct lwp *, struct exec_package *);
static int coredump_wasm32(struct lwp *, struct coredump_iostate *);
static int wasm32_copyargs(struct lwp *, struct exec_package *, struct ps_strings *, char **, void *);
static int wasm_exec_setup_stack(struct lwp *, struct exec_package *);
static int netbsd_wasm32_probe(struct lwp *, struct exec_package *, void *, char *, vaddr_t *);

static struct execsw exec_wasm32_execsw = {
    .es_hdrsz = sizeof (struct exec_wasm_hdr),
    .es_makecmds = exec_wasm32_makecmds,
#if 0
    .u = {
        .elf_probe_func = netbsd_wasm32_probe,
    },
#endif
    .es_emul = &emul_netbsd,
    .es_prio = EXECSW_PRIO_FIRST,
    .es_arglen = 0,
    .es_copyargs = wasm32_copyargs,
    .es_setregs = NULL,
    .es_coredump = coredump_wasm32,
    .es_setup_stack = wasm_exec_setup_stack,
};

static int
exec_wasm32_modcmd(modcmd_t cmd, void *arg)
{

	switch (cmd) {
	case MODULE_CMD_INIT:
		return exec_add(&exec_wasm32_execsw, 1);

	case MODULE_CMD_FINI:
		return exec_remove(&exec_wasm32_execsw, 1);

	default:
		return ENOTTY;
        }
}

static int
exec_wasm32_makecmds(struct lwp *, struct exec_package *)
{
    return (0);
}

static int
coredump_wasm32(struct lwp *lwp, struct coredump_iostate *cookie)
{
    return (0);
}

static int
wasm32_copyargs(struct lwp *lwp, struct exec_package *pack, struct ps_strings *arginfo, char **stackp, void *argp)
{
    return (0);
}


static int
wasm_exec_setup_stack(struct lwp *lwp, struct exec_package *epp)
{
    return (0);
}
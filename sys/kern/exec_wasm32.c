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

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: exec_elf32.c,v 1.143 2019/11/20 19:37:53 pgoyette Exp $");

#ifndef __WASM
#error "This file is only for the wasm architecture"
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/exec.h>
#include <sys/exec_wasm.h>
#include <sys/resourcevar.h>
#include <sys/module.h>
#include <sys/errno.h>
#include <sys/sigtypes.h>

#include <wasm/pcb.h>
#include <wasm/wasm_module.h>


struct wasm32_execpkg_args {
    uint32_t ep_size;   // size of structure in bytes.
    // basically a inline struct ps_strings
    uint32_t ps_nenvstr;
    uint32_t ps_nargvstr;
    // char *ps_argvstr[ps_nargvstr];       // NULL terminated
    // char *ps_argvstr[ps_nenvstr];        // NULL terminated
    // char *strings[]
};

// 8 bytes would be enough for the signature + version
// 64 bytes allows for a peek of the first section.
#define WASM_HDR_SIZE 64
#define WASM_HDR_SIGN_LE 0x6D736100     // \x00asm

int wasm_execbuf_alloc(uint32_t size) __WASM_IMPORT(kern, execbuf_alloc);
int wasm_execbuf_copy(void *kaddr, void *uaddr, uint32_t size) __WASM_IMPORT(kern, execbuf_copy);
void wasm_exec_entrypoint(int argc, char **argv) __WASM_IMPORT(kern, exec_entrypoint);

int wasm_exec_ioctl(int cmd, void *arg) __WASM_IMPORT(kern, exec_ioctl);

#define EXEC_CTL_GET_USTKP 512
#define EXEC_CTL_SET_USTKP 513

static int exec_wasm32_modcmd(modcmd_t, void *);

MODULE(MODULE_CLASS_EXEC, exec_wasm32, NULL);

static int exec_wasm32_makecmds(struct lwp *, struct exec_package *);
static int coredump_wasm32(struct lwp *, struct coredump_iostate *);
static int wasm32_copyargs_pre(struct lwp *, struct exec_package *, struct ps_strings *, char **, void *);
static int wasm32_copyargs_post(struct lwp *, struct wasm32_execpkg_args *, char **stackp, void *argp);
static int wasm_exec_setup_stack(struct lwp *, struct exec_package *);
static int netbsd_wasm32_probe(struct lwp *, struct exec_package *, void *, char *, vaddr_t *);

static void wasm_proc_exec(struct proc *p, struct exec_package *epp);

void wasm_exec_trampline(void *arg);

static struct emul wasm32_syssw = {
    .e_name = "netbsd",
    .e_path = "wasm/netbsd",
    .e_proc_exec = wasm_proc_exec,
};

static struct execsw exec_wasm32_execsw = {
    .es_hdrsz = WASM_HDR_SIZE,
    .es_makecmds = exec_wasm32_makecmds,
#if 0
    .u = {
        .elf_probe_func = netbsd_wasm32_probe,
    },
#endif
    .es_emul = &emul_netbsd,
    .es_prio = EXECSW_PRIO_FIRST,
    .es_arglen = 0,
    .es_copyargs = wasm32_copyargs_pre,
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

#if 0
static int exec_read_direct(struct lwp *l, struct vnode *vp, voff_t off, struct vm_page *pg)
{
    VOP_GETPAGES(vp, off, &pg)
}
#endif

/**
 * Checks if the executable has the binary signature of a WebAssembly Binary.
 */
static int
exec_wasm32_makecmds(struct lwp *l, struct exec_package *epp)
{
    char *hdrstr = epp->ep_hdr; // where is ep_hdr from? and how is it handled/loaded before this function get call?
    char *buf;
    uint32_t ver;
    uint32_t off, bufsz, bsize, fsize;
    int err;

    // the allocated size of ep_hdr size is of exec_maxhdrsz which is set at init by computing the max value from all
    // the defined exec handlers. Its set trough exec_sw->es_hdrsz

    if (*((uint32_t *)hdrstr) != WASM_HDR_SIGN_LE) {
        return ENOEXEC;
    }

    ver = ((uint32_t *)hdrstr)[1];
    if (ver != 0x01) {
        return ENOEXEC;
    }

    // if we reached here we have a binary with a valid signature.
    fsize = epp->ep_vap->va_size;
    bsize = epp->ep_vap->va_blocksize;

    err = wasm_execbuf_alloc(fsize);
    if (err != 0) {
        return err;
    }

    vn_lock(epp->ep_vp, LK_EXCLUSIVE | LK_RETRY);
    err = vn_marktext(epp->ep_vp);
    if (err) {
        VOP_UNLOCK(epp->ep_vp);
    }

    //struct vm_page *pg;

    buf = kmem_alloc(bsize, 0);
    off = 0;
    while (off < fsize) {
        bufsz = MIN(bsize, fsize - off);
        // TODO: do direct read-here
        exec_read((struct lwp *)curlwp, epp->ep_vp, off, buf, bufsz, IO_NODELOCKED);
        wasm_execbuf_copy(buf, (void *)off, bufsz);
        // TODO: make assertion based upon module section content.
        off += bufsz;
    }

    epp->ep_ssize = PAGE_SIZE;

    VOP_UNLOCK(epp->ep_vp);
    
    kmem_free(buf, bsize);

    return (0);
}

static void
wasm_proc_exec(struct proc *p, struct exec_package *epp)
{

}

static int
coredump_wasm32(struct lwp *lwp, struct coredump_iostate *cookie)
{
    printf("%s called", __func__);
    return (0);
}

/**
 * At the point where this is called, we are close to load & compile the binary, but.. we need to setup the memory
 * and alter where the stack begins in order to copy this to the target runtime instance, therefore this need to
 * be copied into a intermediate storage where it's held until wasm_exec_trampline() is executed. 
 */
static int
wasm32_copyargs_pre(struct lwp *lwp, struct exec_package *pack, struct ps_strings *arginfo, char **stackp, void *argp)
{
    printf("%s called", __func__);
    struct pcb *pcb;
    struct switchframe *sf;
    struct wasm32_execpkg_args *args;
    uint32_t bufsz = 0;
    uint32_t pkgsz = 0;
    uint32_t strsz, arrsz;
    char **strarr;
    char *strout;
    char *strp;
    int narg, nenv;

    strp = argp;
    narg = arginfo->ps_nargvstr;
    for (int i = 0; i < narg; i++) {
        strsz = strlen(strp);
        bufsz += strsz + 1;
        strp += strsz + 1;
    }

    nenv = arginfo->ps_nenvstr;
    for (int i = 0; i < nenv; i++) {
        strsz = strlen(strp);
        bufsz += strsz + 1;
        strp += strsz + 1;
    }
    // int argc
    // char *argv[]
    // \0
    // char *env[]
    // \0
    // auxinfo length of bytes.

    arrsz = narg + nenv + 2;
    pkgsz = sizeof(struct wasm32_execpkg_args);
    pkgsz += sizeof(void *) * arrsz;   // +2 for NULL termination
    pkgsz += bufsz;
    args = kmem_zalloc(pkgsz, 0);
    args->ep_size = pkgsz;
    args->ps_nargvstr = narg;
    args->ps_nenvstr = nenv;

    printf("%s execpkg addr %p\n", __func__, args);

    strarr = (char **)(((char *)args) + sizeof(struct wasm32_execpkg_args));
    strout = ((char *)args) + sizeof(struct wasm32_execpkg_args) + (sizeof(void *) * arrsz);
    arginfo->ps_argvstr = strarr;

    strp = argp;
    for (int i = 0; i < narg; i++) {
        strsz = strlen(strp);
        memcpy(strout, strp, strsz);
        *strarr = strout;
        strout += strsz + 1;
        strp += strsz + 1;
    }

    strarr = (char **)(((char *)args) + sizeof(struct wasm32_execpkg_args) + (sizeof(void *) * (narg + 1) ));
    arginfo->ps_envstr = strarr;
    for (int i = 0; i < nenv; i++) {
        strsz = strlen(strp);
        memcpy(strout, strp, strsz);
        *strarr = strout;
        strout += strsz + 1;
        strp += strsz + 1;
    }

    pcb = lwp_getpcb(lwp);

	sf = (struct switchframe *)pcb->pcb_esp;
	sf->sf_esi = (int)wasm_exec_trampline;
	sf->sf_ebx = (int)args;
	sf->sf_eip = (int)lwp_trampoline;

    return (0);
}

static int __noinline
wasm32_copyargs_post(struct lwp *lwp, struct wasm32_execpkg_args *pkg, char **stackp, void *argp)
{
    printf("%s called", __func__);

    // since the strings of both argp and envp is stored in a packed buffer, copy that chunk
    // and then simply convert ptr based on the new address.
    uintptr_t srcoff = (uintptr_t)(((char *)pkg) + offsetof(struct wasm32_execpkg_args, ps_nargvstr));
    uintptr_t dstsoff = (uintptr_t)(*stackp);
    char **strarr;
    uintptr_t strp;

    // translates kernel addresses to user space stack addresses.
    strarr = (char **)(((char *)pkg) + sizeof(struct wasm32_execpkg_args));
    while (true) {
        strp = (uintptr_t)(*strarr);
        if (strp == 0)
            break;
        strp = (strp - srcoff) + dstsoff;
        *strarr = (char *)(strp);
        strarr++;
    }

    strarr = (char **)(((char *)pkg) + sizeof(struct wasm32_execpkg_args) + (sizeof(void *) * (pkg->ps_nargvstr + 1)));
    while (true) {
        strp = (uintptr_t)(*strarr);
        if (strp == 0)
            break;
        strp = (strp - srcoff) + dstsoff;
        *strarr = (char *)(strp);
        strarr++;
    }

    uint32_t stkhdrsz = (pkg->ep_size - offsetof(struct wasm32_execpkg_args, ps_nargvstr));
    copyout(((char *)pkg) + offsetof(struct wasm32_execpkg_args, ps_nargvstr), *stackp, stkhdrsz);

    // TODO: align stack to nearest 8 bytes.

    *stackp = (char *)(dstsoff + stkhdrsz);

    return (0);
}


static int
wasm_exec_setup_stack(struct lwp *lwp, struct exec_package *epp)
{
    printf("%s called", __func__);
    return (0);
}

void
wasm_exec_trampline(void *arg)
{
    struct wasm32_execpkg_args *pkg = (struct wasm32_execpkg_args *)arg;
    char *stackp;
    void *stk_old, *stk_new;
    void *argp;
    int argc = pkg->ps_nargvstr;

    wasm_exec_ioctl(EXEC_CTL_GET_USTKP, &stackp);
    stk_old = stackp;
    // TODO: setup stack
    wasm32_copyargs_post((struct lwp *)curlwp, pkg, &stackp, NULL);

    if (stackp != stk_old) {
        wasm_exec_ioctl(EXEC_CTL_SET_USTKP, stackp);
    }

    argp = (((char *)stackp) + 4);
    
    kmem_free(pkg, pkg->ep_size);

    // should never return
    wasm_exec_entrypoint(argc, argp);
}

void
wasm_exec_prepare_trampoline(void)
{
#if 0
    struct lwp *l;
    struct pcb *pcb;
    struct switchframe *sf;

    l = (struct lwp *)curlwp;
	pcb = lwp_getpcb(l);

	if ((struct lwp *)(pcb->pcb_ebp) != l) {
		printf("PANIC: pcb->pcb_ebp != curlwp");
		__panic_abort();
	}

	sf = (struct switchframe *)pcb->pcb_esp;
	sf->sf_esi = (int)wasm_exec_trampline;
	sf->sf_ebx = (int)NULL;
	sf->sf_eip = (int)lwp_trampoline;
#endif
}
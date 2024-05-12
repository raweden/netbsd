/*	$NetBSD: exec_wasm32.c,v 1.143 2023/07/09 10:13:53 raweden Exp $	*/

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

#include <wasm/../libwasm/libwasm.h>
#include <wasm/../libwasm/wasmloader.h>

#ifdef DEBUG_EXEC
#define COPYPRINTF(s, a, b) printf("%s, %d: copyout%s @%p %zu\n", __func__, \
    __LINE__, (s), (a), (b))
#else
#define COPYPRINTF(s, a, b)
#endif /* DEBUG_EXEC */

#if 0
#define RTLD_ET_EXTERN_MAIN_ENTRYPOINT 1
#define RTLD_ET_EXEC_RTLD 2
#define RTLD_ET_EXEC_START 3

#define WASM_EXEC_PKG_SIGN 0x77614550        // waEP

struct wasm32_execpkg_args {
    uint32_t ep_size;   // size of structure in bytes.
    uint32_t et_sign;   // 
    uint8_t et_type;
    // inline struct ps_strings
	char	**ps_argvstr;	/* first of 0 or more argument strings */
	int	ps_nargvstr;	    /* the number of argument strings */
	char	**ps_envstr;	/* first of 0 or more environment strings */
	int	ps_nenvstr;	        /* the number of environment strings */
    //uint32_t ps_nenvstr;
    //uint32_t ps_nargvstr;
    // char *ps_argvstr[ps_nargvstr];       // NULL terminated
    // char *ps_argvstr[ps_nenvstr];        // NULL terminated
    // char *strings[]
    struct ps_strings *upsarg;              // ps_strings in user-space
    int (*rtld)(uintptr_t *, uintptr_t);
    int (*__start)(void(*)(void), struct ps_strings *);
};
#endif

// 8 bytes would be enough for the signature + version
// 64 bytes allows for a peek of the first section.
#define WASM_HDR_SIZE 64

int wasm_execbuf_alloc(uint32_t size) __WASM_IMPORT(kern, execbuf_alloc);
int wasm_execbuf_copy(void *kaddr, void *uaddr, uint32_t size) __WASM_IMPORT(kern, execbuf_copy);
void wasm_exec_entrypoint(int argc, char **argv, char **envp) __WASM_IMPORT(kern, exec_entrypoint);
void wasm_exec_rtld(int (*)(uintptr_t *, uintptr_t), uintptr_t *, uintptr_t) __WASM_IMPORT(kern, exec_rtld);
void wasm_exec_start(int (*)(void(*)(void), struct ps_strings *), void(*)(void), struct ps_strings *) __WASM_IMPORT(kern, exec_start);

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

// wasm_loader.c
int exec_load_wasm32_binary(struct lwp *, struct exec_vmcmd *);
int wasm_loader_static_exec(struct lwp *, struct exec_vmcmd *);
bool wasm_loader_has_exechdr_in_buf(const char *, size_t, size_t *, size_t *);
int wasm_loader_read_exechdr_early(const char *buf, size_t len, struct wash_exechdr_rt **);

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


static uint32_t
read_uint32_unaligned(const char *p)
{
    union {
        uint32_t val;
        char data[4];
    } i32;

    i32.data[0] = p[0];
    i32.data[1] = p[1];
    i32.data[2] = p[2];
    i32.data[3] = p[3];

    return i32.val;
}

// TODO: the format of exec-hdr does not benefit from being uleb based.. change it to be plainly binary instead!
int
decode_netbsd_exec_hdr_early(const char *data, uint32_t *exec_flags, uint32_t *stack_size_hint, uint32_t *exec_end_offset)
{
    uint32_t lebsz;
    uint32_t strsz;
    uint32_t valsz;
    uint32_t val;
    const char *strp;
    printf("%s data = %p\n", __func__, data);
    uint8_t *p = (uint8_t *)data;
    uint32_t cnt = decodeULEB128(p, &lebsz, NULL, NULL);
    printf("%s cnt = %d\n", __func__, cnt);
    p += lebsz;
    for (int i = 0; i < cnt; i++) {
        strsz = decodeULEB128(p, &lebsz, NULL, NULL);
        printf("%s valsz = %d\n", __func__, strsz);
        p += lebsz;
        strp = (char *)p;
        if (strsz == 0)
            break;
        p += strsz;
        valsz = decodeULEB128(p, &lebsz, NULL, NULL);
        printf("%s valsz = %d\n", __func__, valsz);
        p += lebsz;
        if (valsz == 0)
            break;
        
        if (strncmp(strp, "exec_flags", 10) == 0) {
            val = read_uint32_unaligned((char *)p);
            if (exec_flags)
                *exec_flags = val;
        } else if (strncmp(strp, "stack_size_hint", 15) == 0) {
            val = read_uint32_unaligned((char *)p);
            if (stack_size_hint)
                *stack_size_hint = val;
        } else if (strncmp(strp, "exec_end_offset", 15) == 0) {
            val = read_uint32_unaligned((char *)p);
            if (exec_end_offset)
                *exec_end_offset = val;
        }

        p += valsz;
    }
}

/**
 * Checks if the executable has the binary signature of a WebAssembly Binary.
 *
 * This is invoked by the main exec subrutine in kern_exec.c
 */
static int
exec_wasm32_makecmds(struct lwp *l, struct exec_package *epp)
{
    char *hdrstr = epp->ep_hdr; // where is ep_hdr from? and how is it handled/loaded before this function get call?
    char *buf;
    struct wasm_loader_args *vmcmd_arg;
    uint32_t ver;
    uint32_t off, bufsz, bsize, fsize;
    uint32_t exec_flags;
    uint32_t stack_size_hint;
    uint32_t exec_end_offset;
    struct wash_exechdr_rt *exechdr;
    size_t exechdroff;
    size_t exechdrsz;
    bool is_dyn = false;
    int err;

    exec_flags = 0;
    stack_size_hint = 0;
    exec_end_offset = 0;
    vmcmd_arg = NULL;

    // the allocated size of ep_hdr size is of exec_maxhdrsz which is set at init by computing the max value from all
    // the defined exec handlers. Its set trough exec_sw->es_hdrsz

    if (*((uint32_t *)hdrstr) != WASM_HDR_SIGN_LE) {
        printf("not a wasm executable %d at %p\n", *((uint32_t *)hdrstr), hdrstr);
        return ENOEXEC;
    }

    ver = ((uint32_t *)hdrstr)[1];
    if (ver != 0x01) {
        return ENOEXEC;
    }

    // TODO: move most of this logics into wasm_loader
    // eg.  bool wasm_loader_nbexechdr_in_ephdr(buf, bufsz)
    if (wasm_loader_has_exechdr_in_buf(hdrstr + 8, epp->ep_hdrlen - 8, &exechdroff, &exechdrsz)) {
        
        uint32_t sec_off, load_len;
        char *sec_buf;

        load_len = exechdroff + exechdrsz + 8;
        sec_buf = kmem_alloc(load_len, 0);
        if (sec_buf == NULL) {
            return ENOMEM;
        }

        vn_lock(epp->ep_vp, LK_EXCLUSIVE | LK_RETRY);
        
        exec_read((struct lwp *)curlwp, epp->ep_vp, 0, sec_buf, load_len, IO_NODELOCKED);
        
        VOP_UNLOCK(epp->ep_vp);

        err = wasm_loader_read_exechdr_early(sec_buf + exechdroff + 8, exechdrsz, &exechdr);
        if (err != 0) {
            printf("%s got error = %d from wasm_loader_read_exechdr_early()\n", __func__, err);
        }

        printf("%s exec_type = %d exec_traits = %d stack_size_hint = %d\n", __func__, exechdr ? exechdr->exec_type : 0, exechdr ? exechdr->exec_traits : 0, exechdr ? exechdr->stack_size_hint : 0);
    
        vmcmd_arg = kmem_alloc(sizeof(struct wasm_loader_args), 0);
        if (vmcmd_arg == NULL) {
            return ENOMEM;
        }

        char *namecpy;
        uint32_t namesz = strlen(epp->ep_resolvedname);
        if (namesz > 0) {
            namecpy = kmem_alloc(namesz + 1, 0);
            if (namecpy != NULL)
                strlcpy(namecpy, epp->ep_resolvedname, namesz + 1);
        } else {
            namecpy = NULL;
        }
        
        vmcmd_arg->ep_resolvedname = namecpy;
        vmcmd_arg->ep_resolvednamesz = namesz;
        vmcmd_arg->ep_exechdr = exechdr;

        kmem_free(sec_buf, load_len);
    }
#if 0 // OLD
    if (*(hdrstr + 8) == WASM_SECTION_CUSTOM) {
        char *sec_buf;
        uint32_t sec_off;
        uint8_t *data = (uint8_t *)(hdrstr + 9);
        uint32_t namesz, lebsz = 0;
        uint32_t secsz = decodeULEB128(data, &lebsz, NULL, NULL);
        data += lebsz;
        lebsz = 0;
        namesz = decodeULEB128(data, &lebsz, NULL, NULL);
        data += lebsz;
        if (namesz == 13 && strncmp((char *)data, "rtld.exec-hdr", 13) == 0) {
            printf("%s first section is rtld.exec-hdr of size = %d\n", __func__, secsz);
            
            sec_off = (char *)(data + namesz) - hdrstr;
            uint32_t load_off = 0;
            uint32_t load_len = secsz + sec_off;
            sec_buf = kmem_alloc(load_len, 0);

            printf("%s load_off = %d load_len = %d sec_off = %d\n", __func__, load_off, load_len, sec_off);

            vn_lock(epp->ep_vp, LK_EXCLUSIVE | LK_RETRY);

            exec_read((struct lwp *)curlwp, epp->ep_vp, load_off, sec_buf, load_len, IO_NODELOCKED);

            VOP_UNLOCK(epp->ep_vp);

            exec_flags = 0;
            stack_size_hint = 0;
            exec_end_offset = 0;
            decode_netbsd_exec_hdr_early(sec_buf + sec_off, &exec_flags, &stack_size_hint, &exec_end_offset);

            printf("%s exec_flags = %d stack_size_hint = %d exec_end_offset = %d\n", __func__, exec_flags, stack_size_hint, exec_end_offset);
        
            vmcmd_arg = kmem_alloc(sizeof(struct wasm_loader_args), 0);
            if (vmcmd_arg == NULL) {
                return ENOMEM;
            }

            char *namecpy;
            uint32_t namesz = strlen(epp->ep_resolvedname);
            if (namesz > 0) {
                namecpy = kmem_alloc(namesz + 1, 0);
                if (namecpy != NULL)
                    strlcpy(namecpy, epp->ep_resolvedname, namesz);
            } else {
                namecpy = NULL;
            }
            
            vmcmd_arg->exec_flags = exec_flags;
            vmcmd_arg->stack_size_hint = stack_size_hint;
            vmcmd_arg->exec_end_offset = exec_end_offset;
            vmcmd_arg->ep_resolvedname = namecpy;
            vmcmd_arg->ep_resolvednamesz = namesz;

            kmem_free(sec_buf, load_len);

        } else {
            printf("%s first section is of size %d\n", __func__, secsz);
        }
    }
#endif

    // if we reached here we have a binary with a valid signature.
#if 0
    fsize = epp->ep_vap->va_size;
    bsize = epp->ep_vap->va_blocksize;

    err = wasm_execbuf_alloc(fsize);
    if (err != 0) {
        return err;
    }
#endif

    vn_lock(epp->ep_vp, LK_EXCLUSIVE | LK_RETRY);
    err = vn_marktext(epp->ep_vp);
    if (err) {
        VOP_UNLOCK(epp->ep_vp);
    }

    // TODO: read enough info to determine what runtime the kernel needs to spawn, since we do not want
    // load that extensible into the file, only read the first 4k block, force convention to place the
    // binary note as the first custom section.

    // push into ep_vmcmds, to load the binary when execve_runproc() is called.
    if (vmcmd_arg != NULL) {
        new_vmcmd(&epp->ep_vmcmds, exec_load_wasm32_binary, -1, (vaddr_t)vmcmd_arg, epp->ep_vp, 0, VM_PROT_EXECUTE, 0);
    } else {
        new_vmcmd(&epp->ep_vmcmds, wasm_loader_static_exec, -1, (vaddr_t)NULL, epp->ep_vp, 0, VM_PROT_EXECUTE, 0);
    }

    VOP_UNLOCK(epp->ep_vp);

    epp->ep_ssize = PAGE_SIZE * 2;

#if 0
    //struct vm_page *pg;

    buf = kmem_alloc(bsize, 0);
    off = 0;
    while (off < fsize) {
        bufsz = MIN(bsize, fsize - off);
        // TODO: since everything must be passed trough anyways, we might as well just process
        // the wasm structure here; parsing section and capturing anything of intrest
        // - memory section or imported memory; could change memory limits if compiled with padding.
        // - custom section; find netbsd custom section to extractruntime specific attributes.
        exec_read((struct lwp *)curlwp, epp->ep_vp, off, buf, bufsz, IO_NODELOCKED);
        wasm_execbuf_copy(buf, (void *)off, bufsz);
        // TODO: make assertion based upon module section content.
        off += bufsz;
    }

    epp->ep_ssize = PAGE_SIZE;

    VOP_UNLOCK(epp->ep_vp);
    
    kmem_free(buf, bsize);
#endif

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
    struct wasm32_execpkg_args *exec_arg;
    char	**cpp, *dp, *sp;
	size_t	len;
	void	*nullp;
	long	argc, envc;
	int	error;

	cpp = (char **)*stackp;
	nullp = NULL;
	argc = arginfo->ps_nargvstr;
	envc = arginfo->ps_nenvstr;

	/* argc on stack is long */
	CTASSERT(sizeof(*cpp) == sizeof(argc));

	dp = (char *)(cpp +
	    1 +				/* long argc */
	    argc +			/* char *argv[] */
	    1 +				/* \0 */
	    envc +			/* char *env[] */
	    1) +			/* \0 */
	    pack->ep_esch->es_arglen;	/* auxinfo */
	sp = argp;

	if ((error = copyout(&argc, cpp++, sizeof(argc))) != 0) {
		COPYPRINTF("", cpp - 1, sizeof(argc));
		return error;
	}

	/* XXX don't copy them out, remap them! */
	arginfo->ps_argvstr = cpp; /* remember location of argv for later */

	for (; --argc >= 0; sp += len, dp += len) {
		if ((error = copyout(&dp, cpp++, sizeof(dp))) != 0) {
			COPYPRINTF("", cpp - 1, sizeof(dp));
			return error;
		}
		if ((error = copyoutstr(sp, dp, ARG_MAX, &len)) != 0) {
			COPYPRINTF("str", dp, (size_t)ARG_MAX);
			return error;
		}
	}

	if ((error = copyout(&nullp, cpp++, sizeof(nullp))) != 0) {
		COPYPRINTF("", cpp - 1, sizeof(nullp));
		return error;
	}

	arginfo->ps_envstr = cpp; /* remember location of envp for later */

	for (; --envc >= 0; sp += len, dp += len) {
		if ((error = copyout(&dp, cpp++, sizeof(dp))) != 0) {
			COPYPRINTF("", cpp - 1, sizeof(dp));
			return error;
		}
		if ((error = copyoutstr(sp, dp, ARG_MAX, &len)) != 0) {
			COPYPRINTF("str", dp, (size_t)ARG_MAX);
			return error;
		}

	}

	if ((error = copyout(&nullp, cpp++, sizeof(nullp))) != 0) {
		COPYPRINTF("", cpp - 1, sizeof(nullp));
		return error;
	}

	*stackp = (char *)cpp;

    struct switchframe *sf;
    struct pcb *pcb;
    struct ps_strings tmp;

    pcb = lwp_getpcb(lwp);
    sf = (struct switchframe *)pcb->pcb_esp;

    if (sf->sf_ebx == (int)(NULL) || ((struct wasm32_execpkg_args *)sf->sf_ebx)->et_sign != WASM_EXEC_PKG_SIGN) {

        exec_arg = kmem_zalloc(sizeof(struct wasm32_execpkg_args), 0);
        exec_arg->et_sign = WASM_EXEC_PKG_SIGN;     
        exec_arg->ps_nargvstr = arginfo->ps_nargvstr;
        exec_arg->ps_argvstr = arginfo->ps_argvstr;
        exec_arg->ps_nenvstr = arginfo->ps_nenvstr;
        exec_arg->ps_envstr = arginfo->ps_envstr;
        exec_arg->et_type = RTLD_ET_EXTERN_MAIN_ENTRYPOINT;
        
        sf->sf_esi = (int)wasm_exec_trampline;
        sf->sf_ebx = (int)exec_arg;
        sf->sf_eip = (int)lwp_trampoline;
        printf("%s setting switchframe at %p for lwp %p\n", __func__, sf, lwp);
    } else if (((struct wasm32_execpkg_args *)sf->sf_ebx)->et_sign == WASM_EXEC_PKG_SIGN){
        struct ps_strings *psarg;
        exec_arg = (struct wasm32_execpkg_args *)(sf->sf_ebx);
        psarg = exec_arg->ps_addr;
        if (psarg != NULL) {
            tmp.ps_nargvstr = arginfo->ps_nargvstr;
            tmp.ps_argvstr = arginfo->ps_argvstr;
            tmp.ps_nenvstr = arginfo->ps_nenvstr;
            tmp.ps_envstr = arginfo->ps_envstr;
            copyout(&tmp, psarg, sizeof(struct ps_strings));
        }
    }

	return 0;
#if 0
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
    args->ps_argvstr = strarr;

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
    args->ps_envstr = strarr;

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
#endif
}

/**
 * Converts addresses to user-space and copies the data onto top of stack.
 * Only top-level addresses in the structures can be safely read after this call, the addresses
 * will point to locations in the user-space memory container, the data pointed to are not valid 
 * in kernel space.
 */
static int __noinline
wasm32_copyargs_post(struct lwp *lwp, struct wasm32_execpkg_args *pkg, char **stackp, void *argp)
{
    printf("%s stackp = %p at enter\n", __func__, *stackp);

    // since the strings of both argp and envp is stored in a packed buffer, copy that chunk
    // and then simply convert ptr based on the new address.
    uintptr_t srcoff = (uintptr_t)(((char *)pkg) + offsetof(struct wasm32_execpkg_args, ps_argvstr));
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

    strp = (uintptr_t)(pkg->ps_argvstr);
    strp = (strp - srcoff) + dstsoff;
    pkg->ps_argvstr = (char **)(strp);

    strp = (uintptr_t)(pkg->ps_envstr);
    strp = (strp - srcoff) + dstsoff;
    pkg->ps_envstr = (char **)(strp);

    uint32_t stkhdrsz = (pkg->ep_size - offsetof(struct wasm32_execpkg_args, ps_argvstr));
    copyout(((char *)pkg) + offsetof(struct wasm32_execpkg_args, ps_argvstr), *stackp, stkhdrsz);

    // TODO: align stack to nearest 8 bytes.

    *stackp = (char *)(dstsoff + stkhdrsz);

    printf("%s stackp = %p at exit\n", __func__, *stackp);

    return (0);
}


static int
wasm_exec_setup_stack(struct lwp *lwp, struct exec_package *epp)
{
    printf("%s called", __func__);
    return (0);
}

#define EXEC_IOCTL_EXEC_CTORS 568

void
wasm_exec_trampline(void *arg)
{

    struct wasm32_execpkg_args *pkg = (struct wasm32_execpkg_args *)arg;
    struct ps_strings *psarg;
    void *argp;
    void *envp;
    int error;
    int argc;
#if 0
    wasm_exec_ioctl(EXEC_CTL_GET_USTKP, &stackp);
    stk_old = stackp;
    // TODO: setup stack
    wasm32_copyargs_post((struct lwp *)curlwp, pkg, &stackp, NULL);

    if (stackp != stk_old) {
        wasm_exec_ioctl(EXEC_CTL_SET_USTKP, stackp);
    }

    struct ps_strings *lwp_ps = stk_old;
#endif
    if (pkg->et_type == RTLD_ET_EXTERN_MAIN_ENTRYPOINT) {
        argc = pkg->ps_nargvstr;
        argp = (void *)pkg->ps_argvstr;
        envp = (void *)pkg->ps_envstr;
        
        kmem_free(pkg, pkg->ep_size);

        error = wasm_exec_ioctl(EXEC_IOCTL_EXEC_CTORS, NULL);
        if (error != 0) {
            printf("%s got error = %d running ctors\n", __func__, error);
            // TODO: kill lwp if this happends
        }


        // should never return
        wasm_exec_entrypoint(argc, argp, envp);
    } else if (pkg->et_type == RTLD_ET_EXEC_RTLD) {
        psarg = pkg->ps_addr;
        int (*rtld)(uintptr_t *, uintptr_t) = pkg->rtld;
        
        kmem_free(pkg, pkg->ep_size);

        wasm_exec_rtld(rtld, NULL, 4096);

        // should never return
        wasm_exec_entrypoint(argc, argp, envp);
    } else if (pkg->et_type == RTLD_ET_EXEC_START) {
        psarg = pkg->ps_addr;
        int (*__start)(void(*)(void), struct ps_strings *) = (void *)pkg->__start;
        
        kmem_free(pkg, pkg->ep_size);

        // should never return
        wasm_exec_start(__start, NULL, psarg);
    }
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
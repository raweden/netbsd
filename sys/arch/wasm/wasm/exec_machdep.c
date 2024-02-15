/*	$NetBSD: exec_machdep.c,v 1.3 2022/09/29 06:51:17 skrll Exp $	*/

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas of 3am Software Foundry.
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
__KERNEL_RCSID(0, "$NetBSD: exec_machdep.c,v 1.3 2022/09/29 06:51:17 skrll Exp $");

#include "opt_execfmt.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/exec.h>
#include <sys/namei.h>
#include <sys/resourcevar.h>
#include <sys/exec_elf.h>

#include <uvm/uvm_extern.h>

#include <compat/common/compat_util.h>

#include <sys/dirent.h>
#include <sys/uio.h>

#include <sys/filedesc.h>	// for filedesc_t

#include "arch/wasm/include/wasm_module.h"
#include "arch/wasm/mm/mm.h"

#define WASM_HDR_SIZE 64
#define WASM_HDR_SIGN_LE 0x6D736100     // \x00asm

int wasm_execbuf_alloc(uint32_t size) __WASM_IMPORT(kern, execbuf_alloc);
int wasm_execbuf_copy(void *kaddr, void *uaddr, uint32_t size) __WASM_IMPORT(kern, execbuf_copy);

#if EXEC_ELF32
int
cpu_netbsd_elf32_probe(struct lwp *l, struct exec_package *epp, void *eh0,
    char *itp, vaddr_t *start_p)
{
	(void)compat_elf_check_interp(epp, itp, "rv32");
	return 0;
}
#endif

#if EXEC_ELF64
int
cpu_netbsd_elf64_probe(struct lwp *l, struct exec_package *epp, void *eh0,
    char *itp, vaddr_t *start_p)
{
	(void)compat_elf_check_interp(epp, itp, "rv64");
	return 0;
}
#endif

//#define __WASM_DEBUG_KERN_DYLIB 1


int 
wasm_load_dylib(char *buf, int32_t bufsz) __WASM_EXPORT(wasm_load_dylib)
{
	struct nameidata nd;
	struct pathbuf *pb;
	struct vnode   *vp;
	struct vattr vap;
	struct mm_page *pg;
	struct lwp *l;
	uint32_t pg_flags;
	uint32_t ver;
    uint32_t off, fbufsz, bsize, fsize;
	int error;

	l = (struct lwp *)curlwp;
	// grab the absolute pathbuf here before namei() trashes it.
	pb = pathbuf_create(buf);
	NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF | TRYEMULROOT, pb);

	/* first get the vnode */
	if ((error = namei(&nd)) != 0)
		return error;

	vp = nd.ni_vp;

	/* XXX VOP_GETATTR is the only thing that needs LK_EXCLUSIVE here */
	if ((error = VOP_GETATTR(vp, &vap, l->l_cred)) != 0)
		goto bad1;

	// the allocated size of ep_hdr size is of exec_maxhdrsz which is set at init by computing the max value from all
    // the defined exec handlers. Its set trough exec_sw->es_hdrsz

#if 0
	/* now we have the file, get the exec header */
	error = vn_rdwr(UIO_READ, vp, epp->ep_hdr, epp->ep_hdrlen, 0,
			UIO_SYSSPACE, IO_NODELOCKED, l->l_cred, &resid, NULL);
	if (error)
		goto bad1;
#endif

	// If we have come this far try to load the first chunk of the file and check signature.
	//vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

#if 0
    if (*((uint32_t *)hdrstr) != WASM_HDR_SIGN_LE) {
        printf("not a wasm executable %d at %p\n", *((uint32_t *)hdrstr), hdrstr);
        return ENOEXEC;
    }

    ver = ((uint32_t *)hdrstr)[1];
    if (ver != 0x01) {
        return ENOEXEC;
    }
#endif

    // if we reached here we have a binary with a valid signature.
    fsize = vap.va_size;
    bsize = vap.va_blocksize;

    error = wasm_execbuf_alloc(fsize);
    if (error != 0) {
        return error;
    }

	if (bsize == PAGE_SIZE) {
		buf = kmem_page_alloc(1, 0);
		// bypass file-mapping for exec read
		pg = paddr_to_page(buf);
		pg->flags |= PG_BYPASS_FILE_MAP;
	} else {
		buf = kmem_alloc(bsize, 0);
	}

    //struct vm_page *pg;

	// TODO: since everything must be passed trough anyways, we might as well just process
	// the wasm structure here; parsing section and capturing anything of intrest
	// - memory section or imported memory; could change memory limits if compiled with padding.
	// - custom section; find netbsd custom section to extractruntime specific attributes.

	pg_flags = pg->flags;

    off = 0;
	if (pg != NULL) {

		while (off < fsize) {
			fbufsz = MIN(bsize, fsize - off);

			pg->offset = off;
			pg->flags = pg_flags;
			
			exec_read((struct lwp *)curlwp, vp, off, buf, fbufsz, IO_NODELOCKED);
			wasm_execbuf_copy(buf, (void *)off, fbufsz);
			// TODO: make assertion based upon module section content.
			off += fbufsz;
		}

	} else {

		while (off < fsize) {
			fbufsz = MIN(bsize, fsize - off);
			exec_read((struct lwp *)curlwp, vp, off, buf, fbufsz, IO_NODELOCKED);
			wasm_execbuf_copy(buf, (void *)off, fbufsz);
			// TODO: make assertion based upon module section content.
			off += fbufsz;
		}
	}

    VOP_UNLOCK(vp);

	if (pg != NULL) {
		kmem_page_free(buf, 1);
	} else {
		kmem_free(buf, bsize);
	}

	return 0;

bad1: 
	vput(vp);
	return error;
}

#ifndef __STDOUT_FD_
#define __STDOUT_FD_ 1
#endif

#ifndef __STDERR_FD_
#define __STDERR_FD_ 2
#endif

/**
 * Opens file descriptiors that prints to console API at stdout/stderr if there is no open files at those fds.
 *
 * this should be called before `fd_checkstd()` which points these fds to `/dev/null`
 *
 * console.log is proxied at fd = 1
 * console.error is proxied at fd = 2
 */
int
wasm_lwp_bridge_stdout(bool force)
{
	struct proc *p;
	filedesc_t *fdp;
	file_t *fp;
	fdtab_t *dt;
	struct lwp *l = (struct lwp *)curlwp;
	p = l->l_proc;

	fdp = p->p_fd;
	if (fdp == NULL) {
		printf("process has no fdt open allocated!\n");
		return (0);
	}

	dt = fdp->fd_dt;

	if (dt->dt_ff[__STDOUT_FD_]->ff_file == NULL) {

	}

	return 0;
}
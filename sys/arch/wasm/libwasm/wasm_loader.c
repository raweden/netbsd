/*	$NetBSD: wasm_loader.c,v 1.143 2024-01-23 12:42:27 raweden Exp $	*/

/*
 * Copyright (c) 2024 Jesper Svensson (raweden)
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
__KERNEL_RCSID(0, "$NetBSD: wasmloader.c,v 1.000 2024-01-22 14:51:28 raweden Exp $");

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
#include <sys/types.h>

#include <sys/dirent.h>
#include <sys/uio.h>

#include <sys/filedesc.h>	// for filedesc_t

#include <wasm/pcb.h>
#include <wasm/wasm_module.h>
#include <wasm/wasm/builtin.h>

// FIXME: hacky path
#include <wasm/../mm/mm.h>
#include <wasm/../libwasm/libwasm.h>
#include <wasm/../libwasm/wasmloader.h>

#include "loader.h"
#include "kld_ioctl.h"
#include "rtld.h"

struct wasm_loader_dl_ctx;
struct wa_module_info;
struct wa_section_info;
struct wasm_section_info;
struct wasm_loader_meminfo;
struct wasm_loader_module_dylink_state;

int wasm_execbuf_alloc(uint32_t size) __WASM_IMPORT(kern, execbuf_alloc);
int wasm_execbuf_copy(void *kaddr, void *uaddr, uint32_t size) __WASM_IMPORT(kern, execbuf_copy);
void wasm_exec_entrypoint(int argc, char **argv, char **envp) __WASM_IMPORT(kern, exec_entrypoint);

int __dlsym_early(void *arr, unsigned int count, unsigned int namesz, const char *name, unsigned char type, void *res) __WASM_IMPORT(dlfcn, __dlsym_early);

int wasm_find_dylib(const char *, const char *, char *, int32_t *, struct vnode **vpp);
int wasm_process_chunk_v2(struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module, const char *buf, size_t bufsz, size_t fileoffset, size_t *skiplen);
int wasm_read_section_map_from_memory(struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module, const char *start, const char *end);

int rtld_find_module_memory(struct wasm_loader_dl_ctx *, struct wa_module_info *, int, char *, uint32_t, struct wasm_loader_meminfo *);
int rtld_read_dylink0_subsection_info(struct wasm_loader_dl_ctx *, struct wa_module_info *, struct wa_section_info *);
int rtld_do_extern_reloc(struct lwp *, struct wasm_loader_dl_ctx *, struct wa_module_info *);
int rtld_reloc_place_segments(struct lwp *, struct wasm_loader_dl_ctx *, struct wa_module_info *);
int rtld_dylink0_decode_modules(struct wasm_loader_dl_ctx *, struct wa_module_info *, struct wasm_loader_module_dylink_state *);


struct data_segment_name {
    uint32_t namesz;
    const char *name;
};

static struct data_segment_name data_segment_traits[] = {
    {7, ".rodata"},
    {5, ".data"},
    {4, ".bss"},
    {11, ".init_array"},
    {11, ".fnit_array"},
    {7, ".dynsym"},
    {7, ".dynstr"}
};


#define WA_INLINE_NAME_LEN 28
#define WA_INLINE_SECTIONS 12
#define WA_INLINE_CUSTOM_SECS 8

// pre-processing of wasm-binary

struct wasm_section_info {
    uint8_t wa_type;
    uint32_t wa_size;
    uint32_t wa_offset;     // offset into file
    struct wasm_section_info *wa_next;
};

struct wasm_custom_section_info {
    uint8_t wa_type;
    uint32_t wa_size;
    uint32_t wa_offset;     // offset into file
    struct wasm_section_info *wa_next;
    uint16_t wa_namelen;    // length of name for custom section
    char *wa_name;
    char wa_namebuf[WA_INLINE_NAME_LEN];       // name for custom section.
};

struct wasm_processing_ctx {
    u_int32_t modvers;

    uint32_t bufoff;    // file offset for tmpbuf
    uint32_t bufuse;    // number of bytes in tmpbuf from the previous chunk if any.
    char tmpbuf[256];   // used to temporary hold a piece of the last chunk

    uint32_t seccnt;
    struct wasm_section_info sections[WA_INLINE_SECTIONS];
    uint32_t custom_sec_cnt;
    struct wasm_custom_section_info csecvec[WA_INLINE_CUSTOM_SECS];
    //
    struct wasm_section_info *wa_last_sec;
    struct wasm_section_info *wa_first_section;
    struct wasm_section_info *wa_type_sec;
    struct wasm_section_info *wa_imp_sec;
    struct wasm_section_info *wa_func_sec;
    struct wasm_section_info *wa_tbl_sec;
    struct wasm_section_info *wa_mem_sec;
    struct wasm_section_info *wa_glob_sec;
    struct wasm_section_info *wa_exp_sec;
    struct wasm_section_info *wa_start_sec;
    struct wasm_section_info *wa_elem_sec;
    struct wasm_section_info *wa_code_sec;
    struct wasm_section_info *wa_data_sec;
    struct wasm_section_info *wa_data_cnt_sec;
};

// more direct wasm structs

struct wa_memory_parameters {
    uint32_t min;
    uint32_t max;
    bool shared;
};

struct wa_memory_info {
    uint16_t wa_module_namesz;
    uint16_t wa_namesz;
    char *wa_module_name;
    char *wa_name;
    uint32_t min;
    uint32_t max;
    bool shared;
};

struct wa_data_segment_info {
    uint16_t wa_flags;
    uint8_t  wa_type;
    uint8_t  wa_namesz;
    uint16_t wa_align;
    uint32_t wa_size;
    uint32_t wa_src_offset;
    uint32_t wa_dst_offset;
    const char *wa_name;
};

struct wa_elem_segment_info {
    uint8_t  wa_flags;
    uint8_t  wa_type;
    uint8_t  wa_namesz;
    uint8_t  wa_reftype;
    uint32_t wa_size;
    uint32_t wa_dataSize; // size of encoded vector in bytes
    uint32_t wa_align;
    uint32_t wa_dst_offset;
    const char *wa_name;
};

struct wa_section_info {
    uint16_t wa_flags;
    uint8_t  wa_type;
    uint8_t  wa_namesz;
    uint32_t wa_size;
    uint32_t wa_offset;         // offset into file where data-starts (after type + section-size)
    uint32_t wa_sectionStart;   // offset into file where section-type is located
    struct wa_section_info *wa_next;
    const char *wa_name;
};

struct wasm_loader_module_dylink_state;

struct wa_module_info {
    uint32_t wa_flags;
    uint32_t wa_version;    // version from signature
    uint64_t wa_file_dev;   
    uint64_t wa_file_ino;
    uint32_t wa_filesize;
    char *wa_filebuf;
    struct wash_exechdr_rt *wa_exechdr;
    uint32_t wa_section_count;
    struct wa_section_info **wa_sections;
    struct wa_section_info *wa_first_section;
    struct wa_section_info *wa_last_section;
    uint32_t wa_data_count;
    struct wa_data_segment_info *wa_data_segments;      // array of wa_data_count
    uint32_t wa_elem_count;
    struct wa_elem_segment_info *wa_elem_segments;   // array of wa_elem_count
    uint32_t wa_module_desc_id;                         // internal handle (like fd but references a container at the host side)
    char *wa_filepath;
    char *wa_module_name;
    char *wa_module_vers;
    struct wasm_loader_module_dylink_state *wa_dylink_data;
    struct rtld_memory_descriptor *memdesc;
};

struct wa_module_needed {
    uint32_t wa_flags;
    char *wa_module_name;
    char *wa_module_vers;
    struct wa_module_info *wa_module;
};

union i32_value {
    uint32_t value;
    unsigned char bytes[4];
};

/**
 * Returns a boolean true if a `rtld.exec-hdr` custom section seams to be provided in the given buffer.
 */
bool
wasm_loader_has_exechdr_in_buf(const char *buf, size_t len, size_t *dataoff, size_t *datasz)
{
    uint32_t secsz, namesz, lebsz;
    const uint8_t *ptr = (const uint8_t *)buf;
    const uint8_t *end = ptr + len;

    if (*((uint32_t *)ptr) == WASM_HDR_SIGN_LE) {
        ptr += 8;
    }

    if (*(ptr) != WASM_SECTION_CUSTOM) {
        return false;
    }

    ptr++;

    lebsz = 0;
    secsz = decodeULEB128(ptr, &lebsz, end, NULL);
    ptr += lebsz;

    lebsz = 0;
    namesz = decodeULEB128(ptr, &lebsz, end, NULL);
    ptr += lebsz;

    if (namesz != 13 || strncmp((char *)ptr, "rtld.exec-hdr", 13) != 0) {
        return false;
    }

    if (dataoff)
        *dataoff = (ptr + namesz) - (uint8_t *)(buf);

    if (datasz)
        *datasz = secsz - (namesz + lebsz);

    return true;
}

void
rtld_reloc_exechdr(char *buf)
{
    struct wash_exechdr_rt *hdr;
    struct wasm_exechdr_secinfo *secinfo;
    uint32_t hdrsz, secinfo_cnt;
    const char *min_addr;
    const char *max_addr;

    hdrsz = *((uint32_t *)(buf)); // possible un-aligned
    hdr = (struct wash_exechdr_rt *)hdr;
    min_addr = buf;
    max_addr = buf + hdrsz;
    // string table reloc
    if (hdr->runtime_abi != NULL) {
        hdr->runtime_abi = (void *)((char *)(hdr->runtime_abi) + (uintptr_t)(buf));
    }
    if (hdr->secdata != NULL) {
        hdr->secdata = (void *)((char *)(hdr->secdata) + (uintptr_t)(buf));

        secinfo = hdr->secdata;
        secinfo_cnt = hdr->section_cnt;
        for (int i = 0; i < secinfo_cnt; i++) {
            if (secinfo->name != NULL)
                secinfo->name += (uintptr_t)(buf);
            secinfo++;
        }
    }
}

int
wasm_loader_read_exechdr_early(const char *buf, size_t len, struct wash_exechdr_rt **hdrp)
{
    struct wash_exechdr_rt *hdr;
    struct wasm_exechdr_secinfo *secinfo;
    uint32_t hdrsz, secinfo_cnt;


    hdrsz = *((uint32_t *)(buf)); // possible un-aligned
    if (hdrsz != len || hdrsz > 1024 || hdrp == NULL) {
        return EINVAL;
    }

    hdr = kmem_alloc(hdrsz, 0);
    if (hdr == NULL) {
        return ENOMEM;
    }

    memcpy((void *)hdr, buf, hdrsz);

    rtld_reloc_exechdr((char *)hdr);

    if (hdrp)
        *hdrp = hdr;

    return (0);
}

// TODO: remove this (as its no longer used..)
int
wasm_process_chunk(struct wasm_processing_ctx *ctx, const char *buf, size_t bufsz, size_t fileoffset, size_t *skiplen)
{
    struct wasm_section_info *sec_info;
    uint32_t off = *skiplen;
    uint8_t *data, *data_end, *data_start;
    uint32_t lebsz, reloff;
    uint32_t sec_size, namesz;
    uint8_t *sec_start, *sec_data_start;
    uint32_t bufuse, bufleft;
    uint8_t  sec_type;
    bool backlog = false;
    uint8_t *backlogend;
    char *namep;

    *skiplen = 0;
    data = (uint8_t *)buf;
    data_start = data;
    data_end = (uint8_t *)(buf + bufsz);
    reloff = fileoffset;

    if (off != 0)
        data += off;

    if (fileoffset == 0) {
        uint32_t magic = *((uint32_t *)data);
        uint32_t vers = *((uint32_t *)(data + 4));

        if (magic != WASM_HDR_SIGN_LE) {
            printf("%s invalid magic = %d does not match %d\n", __func__, magic, WASM_HDR_SIGN_LE);
            return EINVAL;
        }

        if (vers != 1) {
            printf("%s invalid version = %d (only version 1 is supported)\n", __func__, vers);
            return EINVAL;
        }

        ctx->modvers = vers;
        data += 8;
        dbg_loading("%s magic = %d module-version = %d", __func__, magic, vers);
    }

    if (ctx->bufuse != 0) {
        bufuse = ctx->bufuse;
        bufleft = (256 - bufleft);
        backlog = true;
        backlogend = (data_start + bufleft);
        memcpy(ctx->tmpbuf + bufuse, data, bufleft);
        data = (uint8_t *)ctx->tmpbuf;
        data_start = data;
        reloff = ctx->bufoff;
    }

    while (backlog || data < data_end) {

        if (!backlog && (data_end - data) < 24) {
            bufuse = (data_end - data);
            memcpy(ctx->tmpbuf, data, bufuse);
            ctx->bufuse = bufuse;
            ctx->bufoff = reloff + (data - data_start);
            printf("%s use backlog skiplen = 0\n", __func__);
            *skiplen = 0;
            break;
        }

        sec_start = data;
        sec_type = *(data);
        data++;
        sec_size = decodeULEB128(data, &lebsz, NULL, NULL);
        data += lebsz;
        sec_data_start = data;

        if (sec_type == WASM_SECTION_CUSTOM) {

            namesz = decodeULEB128(data, &lebsz, NULL, NULL);
            data += lebsz;

            // determine if the whole name is readable, otherwise use backlog
            if ((data + namesz) >= data_end) {
                bufuse = (data_end - sec_start);
                memcpy(ctx->tmpbuf, sec_start, bufuse);
                ctx->bufuse = bufuse;
                ctx->bufoff = reloff + (sec_start - data_start);
                printf("%s use backlog skiplen = 0\n", __func__);
                *skiplen = 0;
                break;
            }

            struct wasm_custom_section_info *cst_info;
            if (ctx->custom_sec_cnt <= WA_INLINE_CUSTOM_SECS) {
                cst_info = &ctx->csecvec[ctx->custom_sec_cnt++];
            } else {
                cst_info = kmem_zalloc(sizeof(struct wasm_custom_section_info), 0);
            }

            cst_info->wa_type = sec_type;
            cst_info->wa_size = sec_size;
            cst_info->wa_offset = reloff + (sec_data_start - data_start);
            sec_info = (struct wasm_section_info *)cst_info;

            if (namesz < WA_INLINE_NAME_LEN) {
                cst_info->wa_name = cst_info->wa_namebuf;
            } else {
                cst_info->wa_name = kmem_alloc(namesz + 1, 0);
                
            }

            strlcpy(cst_info->wa_name, (const char *)data, namesz + 1);
            cst_info->wa_namelen = namesz;
            
            dbg_loading("%s section-type = %d section-size = %d section-offset = %d name-size: %d name = %s", __func__, cst_info->wa_type, cst_info->wa_size, cst_info->wa_offset, cst_info->wa_namelen, cst_info->wa_name);
        } else {
            if (ctx->seccnt <= WA_INLINE_SECTIONS) {
                sec_info = &ctx->sections[ctx->seccnt++];
            } else {
                printf("%s no inline section containers left.. there should only be 12?? at offset %d\n", __func__, reloff);
                return EINVAL;
            }

            sec_info->wa_type = sec_type;
            sec_info->wa_size = sec_size;
            sec_info->wa_offset = reloff + (sec_data_start - data_start);
            dbg_loading("%s section-type = %d section-size = %d section-offset = %d\n", __func__, sec_type, sec_info->wa_size, sec_info->wa_offset);
        }

        if (ctx->wa_first_section == NULL) {
            ctx->wa_first_section = sec_info;
        }

        if (ctx->wa_last_sec != NULL)
            ctx->wa_last_sec->wa_next = sec_info;

        ctx->wa_last_sec = sec_info;

        if (backlog) {
            backlog = false;
            ctx->bufuse = 0;
            data = backlogend;
            data_start = (uint8_t *)buf;
            reloff = fileoffset;
        }

        data = sec_data_start + sec_size;
        if (data > data_end) {
            printf("%s skiplen = %d\n", __func__, (uint32_t)(data - data_end));
            *skiplen = (data - data_end);
            break;
        }
    }

    return 0;
}


struct wasm_loader_dynld_dlsym {
    void *dynsym_start;
    void *dynsym_end;
    uint32_t symbol_size;
    const char *symbol_name;
    uint8_t symbol_type;
    uintptr_t symbol_addr;
};

struct wa_section_info *
wasm_find_section(struct wa_module_info *module, int type, const char *name)
{
    struct wa_section_info **vector;
    struct wa_section_info *sec;
    uint32_t namesz;
    uint32_t count;

    if (type < 0 || type > WASM_SECTION_TAG) {
        return NULL;
    }
    
    count = module->wa_section_count;
    vector = module->wa_sections;
    
    if (type == WASM_SECTION_CUSTOM && name != NULL) {
        namesz = strlen(name);
        for (int i = 0; i < count; i++) {
            sec = vector[i];
            if (sec->wa_type == WASM_SECTION_CUSTOM && sec->wa_namesz == namesz && strncmp(sec->wa_name, name, namesz) == 0) {
                return sec;
            }
        }
    } else {
        for (int i = 0; i < count; i++) {
            sec = vector[i];
            if (sec->wa_type == type) {
                return sec;
            }
        }
    }

    return NULL;
}


struct wasm_loader_dylink_section {
    uint32_t src_offset;
    uint32_t size;
    uint8_t kind;
};

struct wasm_loader_module_dylink_state {
    uint32_t subsection_count;
    struct wasm_loader_dylink_section wa_subsections[8];
    u_int32_t dl_modules_needed_count;
    struct wa_module_needed *dl_modules_needed;
    struct wa_data_segment_info *dl_dlsym;
    struct wasm_module_rt *rtld_modulep;    // in user-memory space
};

struct wasm_loader_dl_ctx {
    struct mm_arena *dl_arena;  // dl loader uses a arena so when kernel related loading is complete and info has been copied, we free the whole arena.
    uint32_t dl_module_count;   // number of used points in dl_modules
    uint32_t dl_modules_size;   // number of points that fit into dl_modules
    struct wa_module_info **dl_modules;
    // reading and processing during loading
    struct mm_page *dl_bufpg;
    uint32_t dl_bufsz;
    void *dl_buf;
    uint32_t dl_backlogsz;      // size of the backlog buffer
    uint32_t dl_backlogoff;     // offset into the file relative to backlog start
    uint32_t dl_backloguse;     // number of bytes in the backlog
    void *dl_backlog;           // address to the backlog buffer
    bool dynld_loaded;          // indicates if the dynld module has been loaded already
    struct ps_strings *libc_ps;
    uintptr_t dl_membase;
    uintptr_t dl_tblbase;
    bool dl_mem_created;
    bool dl_tbl_created;
    struct wasm_loader_tblinfo *dl_memory;
    struct wasm_loader_meminfo *dl_table;
    struct wa_module_info *dl_mainobj;
    uintptr_t __rtld_state_addr;
    uintptr_t __rtld_modvec;
    int (*dl_start)(void(*)(void), struct ps_strings *);
    // rtld data stored before rtld is loaded
    struct rtld_state_common rtld;
};

#define WASM_IMPORT_KIND_FUNC 0x00
#define WASM_IMPORT_KIND_TABLE 0x01
#define WASM_IMPORT_KIND_MEMORY 0x02
#define WASM_IMPORT_KIND_GLOBAL 0x03
#define WASM_IMPORT_KIND_TAG 0x04

static const char __rtldmodule_path[] = "/libexec/ld-wasm.so.1";

int
wasm_loader_load_rtld_module(struct wasm_loader_dl_ctx *dlctx)
{
    struct mm_arena *dl_arena;
    struct wa_module_info *wa_mod;
    struct wa_section_info *sec;
    struct wasm_loader_meminfo meminfo;
    struct nameidata nd;
	struct pathbuf *pb;
	struct vnode   *vp;
	struct vattr vap;
	struct mm_page *pg;
	struct lwp *l;
	uint32_t pg_flags;
	uint32_t ver;
    uint32_t off, fbufsz, bsize, fsize;
    int32_t execfd;
    size_t skiplen;
    uint32_t pathlen;
    char *path;
    char *errstr;
    char *buf;
    char *ptr;
    char *modbuf;
    char *modbufp;
    union {
        struct wasm_loader_cmd_mkbuf mkbuf;
        struct wasm_loader_cmd_wrbuf wrbuf;
        struct wasm_loader_cmd_compile ccbuf;
        struct wasm_loader_cmd_run run;
        struct wasm_loader_dynld_dlsym dlsym;
        struct wasm_loader_cmd_cp_kmem_to_umem cp_kmem;
    } exec_cmd;
    uint32_t bufsz;
    uint32_t pgcnt;
    uint32_t count;
	int error;

    dl_arena = dlctx->dl_arena;
	l = (struct lwp *)curlwp;
    path = PNBUF_GET();
    pathlen = strlen(__rtldmodule_path);
    strlcpy(path, __rtldmodule_path, pathlen + 1);
    pb = pathbuf_assimilate(path);
	NDINIT(&nd, LOOKUP, FOLLOW | TRYEMULROOT, pb);

	/* first get the vnode */
	if ((error = namei(&nd)) != 0) {
        pathbuf_destroy(pb);
        dbg_loading("error: could not find %s error = %d\n", __rtldmodule_path, error);
		return error;
    }

	vp = nd.ni_vp;

    vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

	/* XXX VOP_GETATTR is the only thing that needs LK_EXCLUSIVE here */
	if ((error = VOP_GETATTR(vp, &vap, l->l_cred)) != 0)
		goto error_out;

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

    pgcnt = howmany(fsize, PAGE_SIZE);
    modbuf = kmem_page_alloc(pgcnt, 0);
    if (modbuf == NULL) {
        printf("%s failed to alloc pages for module-size = %d\n", __func__, fsize);
        return ENOMEM;
    }

    modbufp = modbuf;
    
    if (error != 0) {
        return error;
    }

    // TODO: when trying to use a dynld which uses reloc, try to alloc pages to cover it within kernel memory
    //       and perform all ops on that memory, and once its ready read in the essential section into execbuf.

    pg = dlctx->dl_bufpg;
    buf = dlctx->dl_buf;
    bufsz = dlctx->dl_bufsz;
    if (bsize != bufsz) {
        printf("%s block-size does not match temp bufsz", __func__);
        return EINVAL;
    }

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
			
            memcpy(modbufp, buf, fbufsz);
            modbufp += fbufsz;

			// TODO: make assertion based upon module section content.
			off += fbufsz;
		}

	} else {

		while (off < fsize) {
			fbufsz = MIN(bsize, fsize - off);
			exec_read((struct lwp *)curlwp, vp, off, buf, fbufsz, IO_NODELOCKED);

            memcpy(modbufp, buf, fbufsz);
            modbufp += fbufsz;

			// TODO: make assertion based upon module section content.
			off += fbufsz;
		}
	}

    VOP_UNLOCK(vp);

    wa_mod = mm_arena_zalloc_simple(dl_arena, sizeof(struct wa_module_info), NULL);
    if (wa_mod == NULL) {
        VOP_UNLOCK(vp);
        printf("%s could not alloc wa module ctx... \n", __func__);
        return ENOMEM;
    }

    wa_mod->wa_filesize = fsize;
    wa_mod->wa_module_desc_id = 0;
    wa_mod->wa_filebuf = modbuf;

    printf("%s modbuf = %p buf = %p\n", __func__, modbuf, buf);
    skiplen = 0;
    error = wasm_read_section_map_from_memory(dlctx, wa_mod, modbuf, modbuf + fsize);
    if (error != 0) {
        printf("%s got error = %d from wasm_read_section_map_from_memory()\n", __func__, error);
        return EINVAL;
    }

    // this must be done to allow a more flexible access
    count = wa_mod->wa_section_count;
    wa_mod->wa_sections = mm_arena_zalloc_simple(dl_arena, sizeof(void *) * (count + 1), 0);
    sec = wa_mod->wa_first_section;
    for (int i = 0; i < count; i++) {
        wa_mod->wa_sections[i] = sec;
        sec = sec->wa_next;
    }

    // 1. read exechdr if not loaded by exec subrutine
    if (wa_mod->wa_exechdr == NULL) {
        sec = wasm_find_section(wa_mod, WASM_SECTION_CUSTOM, "rtld.exec-hdr");
        if (sec) {
            wasm_loader_read_exechdr_early(modbuf + (sec->wa_offset + 16), sec->wa_size - 16, &wa_mod->wa_exechdr);
        }
    }

    // 2. find memory import
    error = rtld_find_module_memory(dlctx, wa_mod, -1, modbuf, fsize, &meminfo);
    if (error != 0) {
        printf("%s got error = %d from rtld_find_module_memory()\n", __func__, error);
        return EINVAL;
    }
    printf("%s meminfo min = %d max = %d shared = %d min_file_offset = %d min_lebsz = %d max_lebsz %d\n", __func__, meminfo.min, meminfo.max, meminfo.shared, meminfo.min_file_offset, meminfo.min_lebsz, meminfo.max_lebsz);

#if 0
    // 3. find data-segment info (just offset + size)
    sec = wasm_find_section(wa_mod, WASM_SECTION_DATA, NULL);
    if (sec) {
        rtld_read_data_segments_info(dlctx, wa_mod, sec);
    }
#endif

    // 4. find dylink.0 section
    sec = wasm_find_section(wa_mod, WASM_SECTION_CUSTOM, "rtld.dylink.0");
    if (sec) {
        rtld_read_dylink0_subsection_info(dlctx, wa_mod, sec);
    } else {
        printf("missing dylink.0 section.. cannot link!");
        return ENOEXEC;
    }

    rtld_dylink0_decode_modules(dlctx, wa_mod, wa_mod->wa_dylink_data);


    printf("%s meminfo min = %d max = %d shared = %d min_file_offset = %d min_lebsz = %d max_lebsz %d\n", __func__, meminfo.min, meminfo.max, meminfo.shared, meminfo.min_file_offset, meminfo.min_lebsz, meminfo.max_lebsz);

    if (meminfo.min_file_offset != 0) {
        ptr = modbuf + meminfo.min_file_offset;
        encodeULEB128(10, (uint8_t *)ptr, meminfo.min_lebsz);
        ptr += meminfo.min_lebsz;
        encodeULEB128(4096, (uint8_t *)ptr, meminfo.max_lebsz);
    }

    error = rtld_reloc_place_segments(l, dlctx, wa_mod);
    if (error != 0) {
        printf("%s got error = %d after wasm_loader_dynld_do_internal_reloc()\n", __func__, error);
    }

    error = rtld_do_extern_reloc(l, dlctx, wa_mod);
    if (error != 0) {
        printf("%s got error = %d after wasm_loader_dynld_do_extern_reloc()\n", __func__, error);
    }

#if 0
	if (pg != NULL) {
		kmem_page_free(buf, 1);
	} else {
		kmem_free(buf, bsize);
	}
#endif

    exec_cmd.mkbuf.buffer = -1;
    exec_cmd.mkbuf.size = fsize; // TODO: use exec_end_offset later on, but first we must get it up and running..
    error = wasm_exec_ioctl(EXEC_IOCTL_MKBUF, &exec_cmd);
    execfd = exec_cmd.mkbuf.buffer;

    exec_cmd.wrbuf.buffer = execfd;
    exec_cmd.wrbuf.offset = 0;
    exec_cmd.wrbuf.size = fsize;
    exec_cmd.wrbuf.src = modbuf;
    error = wasm_exec_ioctl(EXEC_IOCTL_WRBUF, &exec_cmd);

    errstr = mm_arena_alloc_simple(dl_arena, 256, NULL);
    if (errstr == NULL) {
        printf("%s got error = %d when alloc errstr\n", __func__, error);
        return ENOMEM;
    }

    exec_cmd.run.buffer = execfd;
    exec_cmd.run.flags = 0;
    exec_cmd.run.err = 0;
    exec_cmd.run.strsz = 256;
    exec_cmd.run.strbuf = errstr;
    error = wasm_exec_ioctl(EXEC_IOCTL_RUN_MODULE_AS_DYN_LD, &exec_cmd);
    if (error != 0) {
        printf("%s got error = %d from running module as dynld\n", __func__, error);
    }

    // finding __rtld_state
    struct wa_data_segment_info *dlsym = wa_mod->wa_dylink_data->dl_dlsym;

    if (dlsym != NULL) {
        const char *rtld_sym = "__rtld_state";
        exec_cmd.dlsym.dynsym_start = (void *)dlsym->wa_dst_offset;
        exec_cmd.dlsym.dynsym_end = (void *)(dlsym->wa_dst_offset + dlsym->wa_size);
        exec_cmd.dlsym.symbol_name = rtld_sym;
        exec_cmd.dlsym.symbol_size = strlen(rtld_sym);
        exec_cmd.dlsym.symbol_type = 2;
        exec_cmd.dlsym.symbol_addr = 0;
        error = wasm_exec_ioctl(EXEC_IOCTL_DYNLD_DLSYM_EARLY, &exec_cmd);
        if (error == 0) {
            dlctx->__rtld_state_addr = exec_cmd.dlsym.symbol_addr;
#if 0
            printf("%s found %s at %p\n", __func__, rtld_sym, (void *)exec_cmd.dlsym.symbol_addr);
#endif
            
            // copying the common parts of rtld_state to user-space
            dlctx->rtld.dsovec = (struct wasm_module_rt **)dlctx->__rtld_modvec;
            exec_cmd.cp_kmem.buffer = -1;
            exec_cmd.cp_kmem.dst_offset = (dlctx->__rtld_state_addr);
            exec_cmd.cp_kmem.src = &dlctx->rtld;
            exec_cmd.cp_kmem.size = sizeof(struct rtld_state_common);
            error = wasm_exec_ioctl(EXEC_IOCTL_CP_KMEM_TO_UMEM, &exec_cmd);
            if (error != 0) {
                printf("%s got error = %d from cpy kmem to umem\n", __func__, error);
            }
        } else {
            printf("%s could not find rtld symbol...\n", __func__);
        }
    } else {
        printf("%s could not find rtld symbol... due to wa_dylink_data->dl_dlsym not being set.\n", __func__);
    }

    error = wasm_exec_ioctl(EXEC_IOCTL_RUN_RTLD_INIT, NULL);
    if (error != 0) {
        printf("%s got error = %d from running _rtld_init\n", __func__, error);
    }
    
    if (modbuf != NULL) {
        kmem_page_free(modbuf, pgcnt);
    }

    mm_arena_free_simple(dl_arena, errstr);

    pathbuf_destroy(pb);    // pathbuf calls PNBUF_PUT on path

    vput(vp);

	return 0;

error_out:

    pathbuf_destroy(pb);

	vput(vp);
	return error;
}

int
wasm_loader_find_memory(struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module, struct wa_section_info *section, int execfd, char *buf, uint32_t bufsz, struct wasm_loader_meminfo *meminfo)
{
    union {
        struct wasm_loader_cmd_rdbuf rdbuf;
    } exec_cmd;

    
    const char *module_name;
    const char *name;
    uint32_t module_namesz;
    uint32_t namesz;
    uint8_t kind;
    int error;
    uint32_t symbol_start;
    uint32_t index, count, lebsz;
    uint32_t roff = section->wa_offset;
    uint32_t rlen = section->wa_size;
    uint32_t rend = roff + rlen;
    uint8_t *ptr = (uint8_t *)buf;
    uint8_t *ptr_start = ptr;
    uint8_t *end;
    bool more_data = true;
    bool need_data = true;
    const char *errstr = NULL;
    rlen = MIN(rlen, bufsz);
    end = (uint8_t *)buf + rlen;
    if (rlen == section->wa_size)
        more_data = false;

    exec_cmd.rdbuf.buffer = execfd;
    exec_cmd.rdbuf.dst = buf;
    exec_cmd.rdbuf.src = roff;
    exec_cmd.rdbuf.size = rlen;
    error = wasm_exec_ioctl(EXEC_IOCTL_RDBUF, &exec_cmd);

    /*
    if (*(ptr) != WASM_SECTION_IMPORT) {
        printf("%s buffer start is not import section %02x found\n", __func__, *(ptr));
        return ENOEXEC;
    }
    ptr++;
    */

    index = 0;
    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    while (roff < rend) {

        while (index < count) {
            symbol_start = roff + (ptr - ptr_start);
            lebsz = 0;
            module_namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
            module_name = (const char *)ptr;
            ptr += module_namesz;
            if (ptr >= end) {
                need_data = true;
                break;
            }

            lebsz = 0;
            namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
            name = (const char *)ptr;
            ptr += namesz;
            if (more_data && ptr >= end) {
                need_data = true;
                break;
            }

            kind = *(ptr);
            ptr++;

            if (kind == WASM_IMPORT_KIND_FUNC) {
                uint32_t typeidx = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                if (more_data && ptr >= end) {
                    need_data = true;
                    break;
                }
            } else if (kind == WASM_IMPORT_KIND_TABLE) {
                uint32_t min;
                uint32_t max;
                uint8_t reftype;
                uint8_t limit;
                reftype = *(ptr);
                ptr++;
                limit = *(ptr);
                ptr++;
                if (limit == 0x01) {
                    min = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    if (ptr >= end) {
                        need_data = true;
                        break;
                    }
                    max = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                } else if (limit == 0x00) {
                    min = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                }
                if (more_data && ptr >= end) {
                    need_data = true;
                    break;
                }
            } else if (kind == WASM_IMPORT_KIND_MEMORY) {
                uint32_t min;
                uint32_t max;
                uint32_t min_file_offset;
                uint32_t min_lebsz;
                uint32_t max_lebsz;
                bool shared;
                uint8_t limit;
                limit = *(ptr);
                ptr++;
                min_lebsz = 0;
                max_lebsz = 0;
                min_file_offset = roff + (ptr - (uint8_t *)(buf));
                if (limit == 0x01) {
                    min = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    min_lebsz = lebsz;
                    if (more_data && ptr >= end) {
                        need_data = true;
                        break;
                    }
                    max = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    max_lebsz = lebsz;
                    shared = false;
                } else if (limit == 0x00) {
                    min = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    min_lebsz = lebsz;
                    shared = false;
                } else if (limit == 0x02) {
                    min = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    min_lebsz = lebsz;
                    shared = true;
                } else if (limit == 0x03) {
                    min = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    min_lebsz = lebsz;
                    if (more_data && ptr >= end) {
                        need_data = true;
                        break;
                    }
                    max = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    max_lebsz = lebsz;
                    shared = true;
                }

                if (meminfo) {
                    meminfo->limit = limit;
                    meminfo->min = min;
                    meminfo->max = max;
                    meminfo->min_file_offset = min_file_offset;
                    meminfo->min_lebsz = min_lebsz;
                    meminfo->max_lebsz = max_lebsz;
                    meminfo->shared = shared;
                }

                return (0);
            } else if (kind == WASM_IMPORT_KIND_GLOBAL) {
                uint8_t type;
                uint8_t mutable;
                if (more_data && ptr + 2 >= end) {
                    need_data = true;
                    break;
                }
                type = *(ptr);
                ptr++;
                mutable = *(ptr);
                ptr++;
            } else if (kind == WASM_IMPORT_KIND_TAG) {
                uint8_t attr;
                uint32_t typeidx;
                if (more_data && ptr + 1 >= end) {
                    need_data = true;
                    break;
                }
                attr = *(ptr);
                ptr++;
                typeidx = decodeULEB128(ptr, &lebsz, end, &errstr);
                if (more_data && ptr >= end) {
                    need_data = true;
                    break;
                }
            }

            index++;
        }

        if (more_data == false) {
            break;
        }

        if (need_data) {
            roff = symbol_start;
            exec_cmd.rdbuf.buffer = execfd;
            exec_cmd.rdbuf.dst = buf;
            exec_cmd.rdbuf.src = roff;
            exec_cmd.rdbuf.size = rlen;
            error = wasm_exec_ioctl(EXEC_IOCTL_RDBUF, &exec_cmd);
            ptr = ptr_start;
        }
    }


    return 0;
}

struct wasm_loader_data_segment {
    uint32_t src_offset;
    uint32_t dst_offset;
    uint32_t size;
    const char *name;
    uint16_t dl_flags;
    uint16_t mm_flags;
    uint8_t kind;
};


#if 0
int
wasm_loader_data_segments_info(struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module, struct wa_section_info *section, int execfd, char *buf, uint32_t bufsz)
{
    union {
        struct wasm_loader_cmd_rdbuf rdbuf;
    } exec_cmd;

    struct wasm_loader_data_segment tmpseg;
    struct wasm_loader_data_segment *dseg;
    struct mm_arena *dl_arena;
    struct wa_data_segment_info *wa_data;
    const char *module_name;
    const char *name;
    uint32_t module_namesz;
    uint32_t namesz;
    uint8_t kind;
    int error;
    uint32_t count, lebsz;
    uint32_t roff = section->wa_offset;
    uint32_t rlen = section->wa_size;
    uint32_t rend = roff + rlen;
    uint32_t srcoff;
    uint8_t *ptr = (uint8_t *)buf;
    uint8_t *end = (uint8_t *)buf + bufsz;
    uint8_t *ptr_start = ptr;
    const char *errstr = NULL;
    rlen = MIN(rlen, bufsz);
    dl_arena = dlctx->dl_arena;

    exec_cmd.rdbuf.buffer = execfd;
    exec_cmd.rdbuf.dst = buf;
    exec_cmd.rdbuf.src = roff;
    exec_cmd.rdbuf.size = 48;
    error = wasm_exec_ioctl(EXEC_IOCTL_RDBUF, &exec_cmd);

    /*
    if (*(ptr) != WASM_SECTION_DATA) {
        printf("%s buffer start is not data section %02x found at %d\n", __func__, *(ptr), roff);
        return ENOEXEC;
    }
    ptr++;
    */
    end = (uint8_t *)(buf + 48);
    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    wa_data = mm_arena_zalloc_simple(dl_arena, sizeof(struct wa_data_segment_info) * count, NULL);
    if (wa_data == NULL) {
        return ENOMEM;
    }
    module->wa_data_count = count;
    module->wa_data_segments = wa_data;

    for (int i = 0; i < count; i++) {
        uint32_t size;
        uint32_t kind = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (kind != 0x01) {
            printf("%s found non passive data-segment near %lu\n", __func__, roff + (ptr - ptr_start));
            break;
        }
        size = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        srcoff = roff + (ptr - ptr_start);
        wa_data->wa_type = kind;
        wa_data->wa_size = size;
        wa_data->wa_src_offset = srcoff;

        printf("%s data-segment type = %d size = %d src_offset = %d\n", __func__, wa_data->wa_type, wa_data->wa_size, wa_data->wa_src_offset);
        wa_data++;

        // jumping to next data-segment
        if (i != count - 1) {
            roff = srcoff + size;
            exec_cmd.rdbuf.buffer = execfd;
            exec_cmd.rdbuf.dst = buf;
            exec_cmd.rdbuf.src = roff;
            exec_cmd.rdbuf.size = 48;
            error = wasm_exec_ioctl(EXEC_IOCTL_RDBUF, &exec_cmd);
            ptr = ptr_start;
        }
    }

    // uleb kind (should be u8 I think?)
    // uleb size
    // bytes[size]
    // 
    // only kind == 0x01 is easy to support (we could also put data-segment into a custom section?)

    return 0;
}
#endif


// since element-segment don't have a size indicator in bytes, its kind of messy to read > 1k leb128 just to get to the next element segment.
// rely on info from dylink.0 segment instead.
#if 0
int
wasm_loader_element_segments_info(struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module, struct wa_section_info *section, int execfd, char *buf, uint32_t bufsz)
{
    union {
        struct wasm_loader_cmd_rdbuf rdbuf;
    } exec_cmd;

    struct mm_arena *dl_arena;
    struct wa_elem_segment_info *wa_elem;
    const char *module_name;
    const char *name;
    uint32_t module_namesz;
    uint32_t namesz;
    uint8_t kind;
    int error;
    uint32_t count, lebsz;
    uint32_t roff = section->wa_offset;
    uint32_t rlen = section->wa_size;
    uint32_t rend = roff + rlen;
    uint32_t srcoff;
    uint8_t *ptr = (uint8_t *)buf;
    uint8_t *end = (uint8_t *)buf + bufsz;
    uint8_t *ptr_start = ptr;
    const char *errstr = NULL;
    rlen = MIN(rlen, bufsz);
    dl_arena = dlctx->dl_arena;

    exec_cmd.rdbuf.buffer = execfd;
    exec_cmd.rdbuf.dst = buf;
    exec_cmd.rdbuf.src = roff;
    exec_cmd.rdbuf.size = 48;
    error = wasm_exec_ioctl(EXEC_IOCTL_RDBUF, &exec_cmd);

    /*
    if (*(ptr) != WASM_SECTION_DATA) {
        printf("%s buffer start is not data section %02x found at %d\n", __func__, *(ptr), roff);
        return ENOEXEC;
    }
    ptr++;
    */
    end = (uint8_t *)(buf + 48);
    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    wa_elem = mm_arena_zalloc_simple(dl_arena, sizeof(struct wa_elem_segment_info) * count, NULL);
    if (wa_elem == NULL) {
        printf("%s failed to alloc element segments vector\n", __func__);
        return ENOMEM;
    }
    module->wa_elem_count = count;
    module->wa_elem_segments = wa_elem;

    for (int i = 0; i < count; i++) {
        uint32_t size;
        uint32_t kind = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (kind != 0x01) {
            printf("%s found non passive data-segment near %lu\n", __func__, roff + (ptr - ptr_start));
            break;
        }
        size = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        srcoff = roff + (ptr - ptr_start);
        wa_elem->wa_type = kind;
        wa_elem->wa_size = size;

        printf("%s element-segment type = %d size = %d reftype = %d\n", __func__, wa_elem->wa_type, wa_elem->wa_size, wa_elem->wa_reftype);
        wa_elem++;

        // jumping to next data-segment
        if (i != count - 1) {
            roff = srcoff + size;
            exec_cmd.rdbuf.buffer = execfd;
            exec_cmd.rdbuf.dst = buf;
            exec_cmd.rdbuf.src = roff;
            exec_cmd.rdbuf.size = 48;
            error = wasm_exec_ioctl(EXEC_IOCTL_RDBUF, &exec_cmd);
            ptr = ptr_start;
        }
    }

    // uleb kind (should be u8 I think?)
    // uleb size
    // bytes[size]
    // 
    // only kind == 0x01 is easy to support (we could also put data-segment into a custom section?)

    return 0;
}
#endif

#define NBDL_SUBSEC_MODULES 0x01
#define NBDL_SUBSEC_DATASEG 0x02
#define NBDL_SUBSEC_DATAEXP 0x03
#define NBDL_SUBSEC_FUNCEXP 0x04
#define NBDL_SUBSEC_DATAREQ 0x05
#define NBDL_SUBSEC_FUNCREQ 0x06
#define NBDL_SUBSEC_RLOCEXT 0x07
#define NBDL_SUBSEC_RLOCINT 0x08


struct wasm_loader_dylink_section *
wasm_dylink0_find_subsection(struct wasm_loader_module_dylink_state *dl_state, int kind)
{
    struct wasm_loader_dylink_section *dl_sec;
    uint32_t count = dl_state->subsection_count;
    dl_sec = dl_state->wa_subsections;

    for (int i = 0; i < count; i++) {
        if (dl_sec->kind == kind) {
            return dl_sec;
        }
        dl_sec++;
    }

    return NULL;
}

int
wasm_loader_dylink0_decode_modules(struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module, struct wasm_loader_module_dylink_state *dl_state, int execfd, char *buf, uint32_t bufsz)
{
    struct mm_arena *dl_arena;
    struct wasm_loader_dylink_section *section;
    struct wa_section_info *data_sec;
    struct wa_elem_segment_info *wa_elem;
    struct wa_module_needed *wa_needed;
    struct wa_data_segment_info *arr;
    struct wa_data_segment_info *seg;
    char *vers;
    char *name;
    uint32_t verssz;
    uint32_t namesz;
    uint8_t kind;
    bool more_data, need_data;
    int error;
    uint32_t max_count, count, lebsz;
    uint32_t min_data_off, max_data_off, data_off_start;
    uint32_t roff, rlen, rend, slen;
    uint32_t srcoff;
    uint8_t *ptr, *end, *ptr_start;
    const char *errstr;
    union {
        struct wasm_loader_cmd_rdbuf rdbuf;
    } exec_cmd;

    // find section
    section = wasm_dylink0_find_subsection(dl_state, NBDL_SUBSEC_MODULES);
    if (section == NULL) {
        return EINVAL;
    }

    dl_arena = dlctx->dl_arena;

    roff = section->src_offset;
    slen = section->size;
    rend = roff + slen;
    ptr = (uint8_t *)buf;
    end = (uint8_t *)buf + bufsz;
    ptr_start = ptr;
    errstr = NULL;

    rlen = MIN(slen, bufsz);
    if (rlen == slen) {
        more_data = false;
    }
    

    exec_cmd.rdbuf.buffer = execfd;
    exec_cmd.rdbuf.dst = buf;
    exec_cmd.rdbuf.src = roff;
    exec_cmd.rdbuf.size = rlen;
    error = wasm_exec_ioctl(EXEC_IOCTL_RDBUF, &exec_cmd);

    namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;
    if (namesz != 0) {
        name = mm_arena_alloc_simple(dl_arena, namesz + 1, NULL);
        strlcpy(name, (const char *)ptr, namesz + 1);
        ptr += namesz;
    }
    module->wa_module_name = name;

    vers = NULL;
    verssz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;
    if (verssz != 0) {
        vers = mm_arena_alloc_simple(dl_arena, verssz + 1, NULL);
        strlcpy(vers, (const char *)ptr, verssz + 1);
        ptr += verssz;
    }
    module->wa_module_vers = vers;

    printf("%s module-name = '%s' module-vers = '%s'\n", __func__, module->wa_module_name, module->wa_module_vers);

    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    wa_needed = mm_arena_zalloc_simple(dl_arena, count * sizeof(struct wa_module_needed), NULL);
    if (wa_needed == NULL) {
        printf("%s failed to alloc wa_needed..\n", __func__);
    }

    dl_state->dl_modules_needed_count = count;
    dl_state->dl_modules_needed = wa_needed;

    for (int i = 0; i < count; i++) {
        uint8_t type, vers_type;
        uint32_t vers_count;
        type = *(ptr);
        ptr++;
        namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (namesz != 0) {
            name = mm_arena_alloc_simple(dl_arena, namesz + 1, NULL);
            strlcpy(name, (const char *)ptr, namesz + 1);
            ptr += namesz;
            wa_needed->wa_module_name = name;
        }
        vers_count = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (vers_count != 0) {
            vers_type = *(ptr);
            ptr++;
            if (vers_type == 1) {
                vers = NULL;
                verssz = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                if (verssz != 0) {
                    vers = mm_arena_alloc_simple(dl_arena, verssz + 1, NULL);
                    if (vers != NULL)
                        strlcpy(vers, (const char *)ptr, verssz + 1);
                    ptr += verssz;
                }
                wa_needed->wa_module_vers = vers;
            } else if (vers_type == 2) {
                vers = NULL;
                verssz = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                if (verssz != 0) {
                    ptr += verssz;
                }
                vers = NULL;
                verssz = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                if (verssz != 0) {
                    ptr += verssz;
                }
            }
        }

        printf("%s needed module name '%s' vers = '%s'\n", __func__, wa_needed->wa_module_name, wa_needed->wa_module_vers);
        wa_needed++;
    }

    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    if (count == 0) {
        return 0;
    }

    if (module->wa_elem_segments == NULL) {
        wa_elem = mm_arena_alloc_simple(dl_arena, sizeof(struct wa_elem_segment_info) * count, NULL);
        if (wa_elem == NULL) {
            printf("%s failed to allocate wa_elem_segments vector..\n", __func__);
            return ENOMEM;
        }
        module->wa_elem_segments = wa_elem;
    }

    module->wa_elem_count = count;

    DEBUG_PRINT("%s got %d element-segments\n", __func__, count);

    for (int i = 0; i < count; i++) {
        uint8_t type, vers_type;
        uint32_t segidx, seg_align, seg_size, seg_dataSize;
        type = *(ptr);
        ptr++;
        namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;

        name = NULL;
        if (namesz != 0) {
            name = mm_arena_alloc_simple(dl_arena, namesz + 1, NULL);
            if (name != NULL)
                strlcpy(name, (const char *)ptr, namesz + 1);
            ptr += namesz;
        }
        segidx = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;

        seg_align = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        
        seg_size = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;

        seg_dataSize = 0; // Todo: enable this!

        wa_elem->wa_namesz = namesz;
        wa_elem->wa_name = name;
        wa_elem->wa_size = seg_size;
        wa_elem->wa_align = seg_align;
        wa_elem->wa_dataSize = seg_dataSize;

        dbg_loading("%s element-segment @%p type = %d name = %s (namesz = %d) size = %d (data-size: %d) align = %d\n", __func__, wa_elem, type, wa_elem->wa_name, namesz, wa_elem->wa_size, wa_elem->wa_dataSize, wa_elem->wa_align);

        wa_elem++;
    }

    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    DEBUG_PRINT("%s got %d data-segments\n", __func__, count);

    if (count > 0) {

        arr = mm_arena_zalloc_simple(dl_arena, sizeof(struct wa_data_segment_info) * count, NULL);
        if (arr == NULL) {
            return ENOMEM;
        }

        seg = arr;
        data_sec = wasm_find_section(module, WASM_SECTION_DATA, NULL);
        // data_sec might be null if binary only uses .bss

        min_data_off = 1;                                   // TODO: + lebsz for count
        max_data_off = data_sec ? data_sec->wa_size : 0;    // TODO: - lebsz for count
        data_off_start = data_sec ? data_sec->wa_offset : 0;

        for(int i = 0; i < count; i++) {
            uint32_t dataoff, flags, max_align, size;
            dataoff = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
            flags = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
            max_align = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
            size = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
            namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            name = NULL;

            if (dataoff >= min_data_off && dataoff < max_data_off) {
                dataoff += data_off_start;
            } else if (dataoff != 0) {
                printf("%s data-offset %d for data segment is out of range (min: %d max: %d) %d\n", __func__, dataoff, min_data_off, max_data_off);
                return EINVAL;
            }
            
            if (namesz != 0) {
                name = mm_arena_alloc_simple(dl_arena, namesz + 1, NULL);
                strlcpy(name, (const char *)ptr, namesz + 1);
                ptr += namesz;
            }
            
            seg->wa_flags = flags;
            seg->wa_align = max_align;
            seg->wa_size = size;
            seg->wa_namesz = namesz;
            seg->wa_name = name;
            seg->wa_src_offset = dataoff;

            dbg_loading("%s index = %d name %s size = %d align = %d data-offset = %d\n", __func__, i, seg->wa_name, seg->wa_size, seg->wa_align, seg->wa_src_offset);
            seg++;
        }

        module->wa_data_count = count;
        module->wa_data_segments = arr;
    }

    // uleb count

    // u8 type
    // uleb name-sz 
    // bytes name
    // uleb count
    // u8 vers-type
    // if == 1
    // uleb name-sz bytes name
    // if == 2
    // min | uleb vers-sz bytes vers 
    // max | uleb vers-sz bytes vers

    // followed by element-segment metadata
    // count
    // u8 type
    // uleb name-sz
    // bytes name
    // uleb segidx
    // uleb max_align
    // uleb size

    // followed by data-segment metadata
    // count
    // uleb segidx
    // uleb flags
    // uleb max_align
    // uleb size
    // uleb name-sz
    // bytes name

    return 0;
}

int
wasm_loader_dylink0_subsection_info(struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module, struct wa_section_info *section, int execfd, char *buf, uint32_t bufsz)
{
    union {
        struct wasm_loader_cmd_rdbuf rdbuf;
    } exec_cmd;
    struct mm_arena *dl_arena;
    struct wasm_loader_module_dylink_state *dl_state;
    struct wasm_loader_dylink_section *dl_sec;
    const char *module_name;
    const char *name;
    uint32_t module_namesz;
    uint32_t namesz;
    uint8_t kind;
    int error;
    uint32_t count, lebsz;
    uint32_t roff = section->wa_offset;
    uint32_t rlen = section->wa_size;
    uint32_t rend = roff + rlen;
    uint32_t srcoff;
    uint8_t *ptr = (uint8_t *)buf;
    uint8_t *end = (uint8_t *)buf + bufsz;
    uint8_t *ptr_start = ptr;
    const char *errstr = NULL;
    rlen = MIN(rlen, bufsz);
    dl_arena = dlctx->dl_arena;

    exec_cmd.rdbuf.buffer = execfd;
    exec_cmd.rdbuf.dst = buf;
    exec_cmd.rdbuf.src = roff;
    exec_cmd.rdbuf.size = 64;
    error = wasm_exec_ioctl(EXEC_IOCTL_RDBUF, &exec_cmd);

    end = (uint8_t *)(buf + 64);

    namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;
    if (namesz != 13 || strncmp((const char *)ptr, "rtld.dylink.0", namesz) != 0) {
        printf("%s not a rtld.dylink.0 section..\n", __func__);
        return EINVAL;
    }
    ptr += namesz;

    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    if (count > 8) {
        printf("%s current ABI specifies 8 segment types and all unique, why is there %d\n",__func__, count);
        return EINVAL;
    }
    
    dl_state = mm_arena_zalloc_simple(dl_arena, sizeof(struct wasm_loader_module_dylink_state), NULL);
    if (!dl_state) {
        printf("%s failed to alloc dl_state\n", __func__);
        return ENOMEM;
    }

    dl_state->subsection_count = count;
    dl_sec = dl_state->wa_subsections;

    for (int i = 0; i < count; i++) {
        uint32_t size;
        uint32_t kind = *(ptr);
        ptr++;
        size = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        srcoff = roff + (ptr - ptr_start);
        dl_sec->kind = kind;
        dl_sec->size = size;
        dl_sec->src_offset = srcoff;

        printf("%s dylink-subsection kind = %d size = %d src_offset = %d\n", __func__, dl_sec->kind, dl_sec->size, dl_sec->src_offset);

        dl_sec++;
        // jumping to next data-segment
        if (i != count - 1) {
            roff = srcoff + size;
            exec_cmd.rdbuf.buffer = execfd;
            exec_cmd.rdbuf.dst = buf;
            exec_cmd.rdbuf.src = roff;
            exec_cmd.rdbuf.size = 32;
            error = wasm_exec_ioctl(EXEC_IOCTL_RDBUF, &exec_cmd);
            ptr = ptr_start;
            end = ptr_start + 32;
        }
    }

    module->wa_dylink_data = dl_state;

    return 0;
}

// TODO: add parameter for more_data in call (to mark eof)
int
wasm_process_chunk_v2(struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module, const char *buf, size_t bufsz, size_t fileoffset, size_t *skiplen)
{
    struct mm_arena *dl_arena;
    struct wa_section_info *sec_info;
    uint32_t off = *skiplen;
    uint8_t *data, *data_end, *data_start;
    uint32_t lebsz, reloff;
    uint32_t sec_size, namesz;
    uint8_t *sec_start, *sec_data_start;
    uint32_t bufuse, bufleft;
    uint8_t  sec_type;
    bool backlog = false;
    uint8_t *backlogend;
    char *namep;
    char *wa_name;

    *skiplen = 0;
    data = (uint8_t *)buf;
    data_start = data;
    data_end = (uint8_t *)(buf + bufsz);
    reloff = fileoffset;
    dl_arena = dlctx->dl_arena;

    if (off != 0)
        data += off;

    if (fileoffset == 0) {
        uint32_t magic = *((uint32_t *)data);
        uint32_t vers = *((uint32_t *)(data + 4));

        if (magic != WASM_HDR_SIGN_LE) {
            printf("%s invalid magic = %d does not match %d\n", __func__, magic, WASM_HDR_SIGN_LE);
            return EINVAL;
        }

        if (vers != 1) {
            printf("%s invalid version = %d (only version 1 is supported)\n", __func__, vers);
            return EINVAL;
        }

        module->wa_version = vers;
        data += 8;
        printf("%s magic = %d module-version = %d", __func__, magic, vers);
    }

    if (dlctx->dl_backloguse != 0) {
        bufuse = dlctx->dl_backloguse;
        bufleft = (256 - bufleft);
        backlog = true;
        backlogend = (data_start + bufleft);
        memcpy(dlctx->dl_backlog + bufuse, data, bufleft);
        data = (uint8_t *)dlctx->dl_backlog;
        data_start = data;
        reloff = dlctx->dl_backlogoff;
    }

    while (backlog || data < data_end) {

        if (!backlog && (data_end - data) < 24) {
            bufuse = (data_end - data);
            memcpy(dlctx->dl_backlog, data, bufuse);
            dlctx->dl_backloguse = bufuse;
            dlctx->dl_backlogoff = reloff + (data - data_start);
            printf("%s use backlog skiplen = 0\n", __func__);
            *skiplen = 0;
            break;
        }

        sec_start = data;
        sec_type = *(data);
        data++;
        sec_size = decodeULEB128(data, &lebsz, NULL, NULL);
        data += lebsz;
        sec_data_start = data;

        if (sec_type == WASM_SECTION_CUSTOM) {

            namesz = decodeULEB128(data, &lebsz, NULL, NULL);
            data += lebsz;

            // determine if the whole name is readable, otherwise use backlog
            if ((data + namesz) >= data_end) {
                bufuse = (data_end - sec_start);
                memcpy(dlctx->dl_backlog, sec_start, bufuse);
                dlctx->dl_backloguse = bufuse;
                dlctx->dl_backlogoff = reloff + (sec_start - data_start);
                printf("%s use backlog skiplen = 0\n", __func__);
                *skiplen = 0;
                break;
            }

            sec_info = mm_arena_alloc_simple(dl_arena, sizeof(struct wa_section_info), NULL);
            if (sec_info == NULL) {
                return ENOMEM;
            }
            sec_info->wa_type = sec_type;
            sec_info->wa_size = sec_size;
            sec_info->wa_offset = reloff + (sec_data_start - data_start);
            sec_info->wa_sectionStart = reloff + (sec_start - data_start);

            if (namesz > 255) {
                printf("ERROR: %s possible a namesz too large? %d\n", __func__, namesz);
            }
            
            wa_name = mm_arena_alloc_simple(dl_arena, namesz + 1, NULL);
            if (sec_info == NULL) {
                return ENOMEM;
            }
            strlcpy(wa_name, (const char *)data, namesz + 1);
            sec_info->wa_name = wa_name;
            sec_info->wa_namesz = namesz;
            
            dbg_loading("%s section-type = %d section-size = %d section-offset = %d name-size: %d name = %s", __func__, sec_info->wa_type, sec_info->wa_size, sec_info->wa_offset, sec_info->wa_namesz, sec_info->wa_name);
        } else {

            sec_info = mm_arena_alloc_simple(dl_arena, sizeof(struct wa_section_info), NULL);
            if (sec_info == NULL) {
                return ENOMEM;
            }

            sec_info->wa_type = sec_type;
            sec_info->wa_size = sec_size;
            sec_info->wa_offset = reloff + (sec_data_start - data_start);
            sec_info->wa_sectionStart = reloff + (sec_start - data_start);
            dbg_loading("%s section-type = %d section-size = %d section-offset = %d\n", __func__, sec_type, sec_info->wa_size, sec_info->wa_offset);
        }

        if (module->wa_first_section == NULL) {
            module->wa_first_section = sec_info;
        }

        if (module->wa_last_section != NULL)
            module->wa_last_section->wa_next = sec_info;

        module->wa_last_section = sec_info;
        module->wa_section_count++;

        if (backlog) {
            backlog = false;
            dlctx->dl_backloguse = 0;
            data = backlogend;
            data_start = (uint8_t *)buf;
            reloff = fileoffset;
        }

        data = sec_data_start + sec_size;
        if (data > data_end) {
            printf("%s skiplen = %d\n", __func__, (uint32_t)(data - data_end));
            *skiplen = (data - data_end);
            break;
        }
    }
    
    return 0;
}

/**
 * 
 */
int
wasm_read_section_map_from_memory(struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module, const char *buf, const char *bufend)
{
    struct mm_arena *dl_arena;
    struct wa_section_info *sec_info;
    uint8_t *ptr, *ptr_start, *end;
    uint32_t lebsz;
    uint32_t sec_size, namesz;
    uint8_t *sec_start, *sec_data_start;
    uint32_t bufuse, bufleft;
    uint8_t  sec_type;
    bool backlog = false;
    uint8_t *backlogend;
    char *namep;
    char *wa_name;

    ptr = (uint8_t *)buf;
    ptr_start = ptr;
    end = bufend;
    dl_arena = dlctx->dl_arena;


    uint32_t magic = *((uint32_t *)ptr);
    uint32_t vers = *((uint32_t *)(ptr + 4));

    if (magic != WASM_HDR_SIGN_LE) {
        printf("%s invalid magic = %d does not match %d\n", __func__, magic, WASM_HDR_SIGN_LE);
        return EINVAL;
    }

    if (vers != 1) {
        printf("%s invalid version = %d (only version 1 is supported)\n", __func__, vers);
        return EINVAL;
    }

    module->wa_version = vers;
    ptr += 8;
    printf("%s magic = %d module-version = %d", __func__, magic, vers);

    while (ptr < end) {

        sec_start = ptr;
        sec_type = *(ptr);
        ptr++;
        sec_size = decodeULEB128(ptr, &lebsz, NULL, NULL);
        ptr += lebsz;
        sec_data_start = ptr;

        if (sec_type == WASM_SECTION_CUSTOM) {

            namesz = decodeULEB128(ptr, &lebsz, NULL, NULL);
            ptr += lebsz;

            // determine if the whole name is readable, otherwise use backlog
            if ((ptr + namesz) >= end) {
                return EOVERFLOW;
            }

            sec_info = mm_arena_alloc_simple(dl_arena, sizeof(struct wa_section_info), NULL);
            if (sec_info == NULL) {
                return ENOMEM;
            }
            sec_info->wa_type = sec_type;
            sec_info->wa_size = sec_size;
            sec_info->wa_offset = (sec_data_start - ptr_start);
            sec_info->wa_sectionStart = (sec_start - ptr_start);
            
            wa_name = mm_arena_alloc_simple(dl_arena, namesz + 1, NULL);
            if (sec_info == NULL) {
                return ENOMEM;
            }
            strlcpy(wa_name, (const char *)ptr, namesz + 1);
            sec_info->wa_name = wa_name;
            sec_info->wa_namesz = namesz;
            
            dbg_loading("%s section-type = %d section-size = %d section-offset = %d name-size: %d name = %s", __func__, sec_info->wa_type, sec_info->wa_size, sec_info->wa_offset, sec_info->wa_namesz, sec_info->wa_name);
        } else {

            sec_info = mm_arena_alloc_simple(dl_arena, sizeof(struct wa_section_info), NULL);
            if (sec_info == NULL) {
                return ENOMEM;
            }

            sec_info->wa_type = sec_type;
            sec_info->wa_size = sec_size;
            sec_info->wa_offset = (sec_data_start - ptr_start);
            sec_info->wa_sectionStart = (sec_start - ptr_start);
            dbg_loading("%s section-type = %d section-size = %d section-offset = %d\n", __func__, sec_type, sec_info->wa_size, sec_info->wa_offset);
        }

        if (module->wa_first_section == NULL) {
            module->wa_first_section = sec_info;
        }

        if (module->wa_last_section != NULL)
            module->wa_last_section->wa_next = sec_info;

        module->wa_last_section = sec_info;
        module->wa_section_count++;

        ptr = sec_data_start + sec_size;
    }
    
    return 0;
}

struct wa_module_info *
wasm_loader_find_loaded(struct wasm_loader_dl_ctx *dlctx, const char *name, const char *ver)
{
    uint32_t count = dlctx->dl_module_count;
    struct wa_module_info **modules = dlctx->dl_modules;
    for (int i = 0; i < count; i++) {
        struct wa_module_info *mod = modules[i];
        if (strcmp(mod->wa_module_name, name) == 0) { // && (ver == NULL || strcmp(mod->wa_module_vers, ver) == 0)
            return mod;
        }
    }

    return NULL;
}

int
wasm_loader_open_vnode(struct lwp *l, const char *filepath, struct vnode **vpp)
{
    struct nameidata nd;
	struct pathbuf *pb;
    uint32_t pathlen;
    char *path;
	int error;

    path = PNBUF_GET();
    pathlen = strlen(filepath);
    strlcpy(path, filepath, pathlen + 1);
    pb = pathbuf_assimilate(path);
	NDINIT(&nd, LOOKUP, FOLLOW | TRYEMULROOT, pb);

    // first get the vnode
    error = namei(&nd);

    if (error == 0 && vpp) {
        *vpp = nd.ni_vp;
    }
    
    pathbuf_destroy(pb);

    return error;
}

int
wasm_loader_dynld_load_module(struct lwp *l, struct wasm_loader_dl_ctx *dlctx, struct vnode *vp, const char *filepath, struct wash_exechdr_rt *exechdr, struct wa_module_needed *dep)
{
    struct vattr vap;
    struct wa_module_info *wa_mod;
    struct wa_section_info *sec;
    struct wasm_loader_args *args;
    struct mm_arena *dl_arena;
    struct wasm_loader_module_dylink_state *dl_state;
    size_t skiplen;
    char *buf;
    uint32_t ver;
    uint32_t count;
    uint32_t off, bufsz, bsize, fsize;
    uint32_t exec_flags = 0;
    uint32_t exec_end_offset = 0;
    uint32_t stack_size_hint = 0;
    int32_t exec_bufid;
    bool is_main;
    bool is_dyn = false;
    int error;
    union {
        struct wasm_loader_cmd_mkbuf mkbuf;
        struct wasm_loader_cmd_wrbuf wrbuf;
        struct wasm_loader_cmd_rdbuf rdbuf;
        struct wasm_loader_cmd_cp_buf_to_umem cp_buf;
        struct wasm_loader_cmd_cp_buf_to_umem cp_kmem;
        struct wasm_loader_cmd_mk_umem mkmem;
        struct wasm_loader_cmd_umem_grow mgrow;
        struct wasm_loader_cmd_rloc_leb rloc_leb;
        struct wasm_loader_cmd_rloc_i32 rloc_i32;
    } exec_cmd;

    vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

    error = VOP_GETATTR(vp, &vap, l->l_cred);
    if (error != 0) {
        VOP_UNLOCK(vp);
        printf("%s error when VOP_GETATTR() called %d", __func__, error);
        return error;
    }

    dl_arena = dlctx->dl_arena;
    fsize = vap.va_size;
    bsize = vap.va_blocksize;

#if 0
    if (cmd->ev_addr != 0) {
        args = (struct wasm_loader_args *)cmd->ev_addr;
        exec_flags = args->exec_flags;
        exec_end_offset = args->exec_end_offset;
        stack_size_hint = args->stack_size_hint;
        if (args->ep_resolvedname != NULL) {
            filepath = mm_arena_zalloc_simple(dl_arena, args->ep_resolvednamesz + 1, NULL);
            strlcpy(filepath, args->ep_resolvedname, args->ep_resolvednamesz + 1);
            kmem_free(args->ep_resolvedname, 0);
        }
    } else {
        exec_end_offset = fsize;
        filepath = NULL;
    }
#endif

    wa_mod = mm_arena_zalloc_simple(dl_arena, sizeof(struct wa_module_info), NULL);
    if (wa_mod == NULL) {
        VOP_UNLOCK(vp);
        printf("%s could not alloc wa module ctx... \n", __func__);
        return ENOMEM;
    }

    wa_mod->wa_file_ino = vap.va_fileid;
    wa_mod->wa_file_dev = vap.va_fsid;
    wa_mod->wa_filesize = fsize;
    wa_mod->wa_filepath = (char *)filepath;
    if (exechdr != NULL) {
        wa_mod->wa_exechdr = exechdr;
    }

    is_main = false;
    if (dlctx->dl_mainobj == NULL) {
        dlctx->dl_mainobj = wa_mod;
        is_main = true;
    }

    exec_cmd.mkbuf.buffer = -1;
    exec_cmd.mkbuf.size = fsize; // TODO: use exec_end_offset later on, but first we must get it up and running..
    error = wasm_exec_ioctl(EXEC_IOCTL_MKBUF, &exec_cmd);
    exec_bufid = exec_cmd.mkbuf.buffer;
    wa_mod->wa_module_desc_id = exec_bufid;
    //error = wasm_execbuf_alloc(fsize);
    if (error != 0) {
        VOP_UNLOCK(vp);
        printf("%s error when wasm_execbuf_alloc() called %d\n", __func__, error);
        return error;
    }

    printf("%s starting to load wasm executable of size = %d\n", __func__, fsize);

    skiplen = 0;
    if (dlctx->dl_buf == NULL) {

        if (bsize == PAGE_SIZE) {
            struct mm_page *pg;
            buf = kmem_page_alloc(1, 0);
            // bypass file-mapping for exec read
            pg = paddr_to_page(buf);
            pg->flags |= PG_BYPASS_FILE_MAP;
            dlctx->dl_bufpg = pg;
        } else {
            buf = kmem_alloc(bsize, 0);
            if (buf == NULL) {
                VOP_UNLOCK(vp);
                printf("%s ENOMEM when kmem_alloc() called\n", __func__);
                return ENOMEM;
            }
        }

        dlctx->dl_buf = buf;
        dlctx->dl_bufsz = bsize;
    } else {
        buf = dlctx->dl_buf;
        bufsz = dlctx->dl_bufsz;
    }
    off = 0;
    while (off < fsize) {
        bufsz = MIN(bsize, fsize - off);
        error = exec_read((struct lwp *)curlwp, vp, off, buf, bufsz, IO_NODELOCKED);
        if (error != 0) {
            printf("%s got error = %d from exec_read() \n", __func__, error);
        }

        // processing during loading
        if (skiplen < bufsz) {
            error = wasm_process_chunk_v2(dlctx, wa_mod, buf, bufsz, off, &skiplen);
            //wasm_process_chunk(wa_mod, buf, bufsz, off, &skiplen);
        } else {
            skiplen -= bufsz;
        }

        exec_cmd.wrbuf.buffer = exec_bufid;
        exec_cmd.wrbuf.offset = off;
        exec_cmd.wrbuf.size = bufsz;
        exec_cmd.wrbuf.src = buf;
        error = wasm_exec_ioctl(EXEC_IOCTL_WRBUF, &exec_cmd);
        // TODO: make assertion based upon module section content.
        off += bufsz;
    }

    VOP_UNLOCK(vp);

    count = wa_mod->wa_section_count;
    wa_mod->wa_sections = mm_arena_zalloc_simple(dl_arena, sizeof(void *) * (count + 1), 0);
    sec = wa_mod->wa_first_section;
    for (int i = 0; i < count; i++) {
        wa_mod->wa_sections[i] = sec;
        sec = sec->wa_next;
    }

    buf = dlctx->dl_buf;
    bufsz = dlctx->dl_bufsz;

    // 1. read exechdr if not loaded by exec subrutine
    if (wa_mod->wa_exechdr == NULL) {
        sec = wasm_find_section(wa_mod, WASM_SECTION_CUSTOM, "rtld.exec-hdr");
        if (sec) {
            exec_cmd.rdbuf.buffer = exec_bufid;
            exec_cmd.rdbuf.src = sec->wa_offset + 14;
            exec_cmd.rdbuf.size = sec->wa_size - 14;
            exec_cmd.rdbuf.dst = buf;
            error = wasm_exec_ioctl(EXEC_IOCTL_RDBUF, &exec_cmd);
            if (error == 0) {
                error = wasm_loader_read_exechdr_early(buf, sec->wa_size - 14, &wa_mod->wa_exechdr);
                if (error != 0) {
                    printf("%s got error = %d from wasm_loader_read_exechdr_early()\n", __func__, error);
                }
            } else {
                printf("%s got error = %d from wasm_exec_ioctl()\n", __func__, error);
            }
        }
    }

    // 2. find memory import
    struct wasm_loader_meminfo meminfo;
    sec = wasm_find_section(wa_mod, WASM_SECTION_IMPORT, NULL);
    if (sec) {
        wasm_loader_find_memory(dlctx, wa_mod, sec, exec_bufid, buf, bsize, &meminfo);
        printf("%s meminfo min = %d max = %d shared = %d min_file_offset = %d min_lebsz = %d max_lebsz %d\n", __func__, meminfo.min, meminfo.max, meminfo.shared, meminfo.min_file_offset, meminfo.min_lebsz, meminfo.max_lebsz);
        if (is_main) {
            struct rtld_memory_descriptor *memdesc = mm_arena_zalloc_simple(dl_arena, sizeof(struct rtld_memory_descriptor), NULL);
            if (memdesc != NULL) {
                memdesc->initial = meminfo.min;
                memdesc->maximum = meminfo.max;
                memdesc->shared = meminfo.shared;
                wa_mod->memdesc = memdesc;
            }
        }
    }

#if 0
    // 3. find data-segment info (just offset + size)
    sec = wasm_find_section(wa_mod, WASM_SECTION_DATA, NULL);
    if (sec) {
        wasm_loader_data_segments_info(dlctx, wa_mod, sec, exec_bufid, buf, bsize);
    }
#endif

    // 4. read dylink.0 section
    sec = wasm_find_section(wa_mod, WASM_SECTION_CUSTOM, "rtld.dylink.0");
    if (sec) {
        wasm_loader_dylink0_subsection_info(dlctx, wa_mod, sec, exec_bufid, buf, bsize);
    } else {
        printf("missing dylink.0 section.. cannot link!");
        return ENOEXEC;
    }

    wasm_loader_dylink0_decode_modules(dlctx, wa_mod, wa_mod->wa_dylink_data, exec_bufid, buf, bufsz);

    
    if (dep) {
        dep->wa_module = wa_mod;
    }

    count = dlctx->dl_module_count++;
    if (count >= dlctx->dl_modules_size) {
        uint32_t oldsz = dlctx->dl_modules_size;
        uint32_t newsz = oldsz + 8;
        void *old = dlctx->dl_modules;
        void *new = mm_arena_zalloc_simple(dl_arena, newsz * sizeof(void *), NULL);
        if (new == NULL) {
            printf("%s failed to allocate new memory for dlctx module vector\n", __func__);
            return ENOMEM;
        }
        dlctx->dl_modules_size = newsz;
        memcpy(new, old, oldsz * sizeof(void *));
        dlctx->dl_modules = new;
        mm_arena_free_simple(dl_arena, old);
    }
    dlctx->dl_modules[count] = wa_mod;

    if (wa_mod->wa_dylink_data == NULL) {
        printf("%s wa_dylink_data is NULL\n", __func__);
        return ENOEXEC;
    }

    dl_state = wa_mod->wa_dylink_data;
    count = dl_state->dl_modules_needed_count;
    struct wa_module_needed *sub_dep = dl_state->dl_modules_needed;
    for (int i = 0; i < count; i++) {
        struct wa_module_info *depmod = wasm_loader_find_loaded(dlctx, sub_dep->wa_module_name, sub_dep->wa_module_vers);
        if (depmod != NULL) {
            sub_dep->wa_module = depmod;
        } else {
            struct vnode *sub_vp;
            int32_t outbufsz = bufsz;
            char *sfilepath;
            //strncpy(buf, sub_dep->wa_module_name, strlen(sub_dep->wa_module_name) + 1);
            sub_vp = NULL;
            sfilepath = NULL;
            error = wasm_find_dylib(sub_dep->wa_module_name, sub_dep->wa_module_vers, buf, &outbufsz, &sub_vp);
            if (error != 0) {
                printf("%s got error = %d from wasm_find_dylib()\n",__func__, error);
            }
            if (outbufsz > 0) {
                sfilepath = mm_arena_alloc_simple(dl_arena, outbufsz + 1, NULL);
                if (sfilepath != NULL)
                    strlcpy(sfilepath, buf, outbufsz + 1);
                printf("%s filepath = '%s'\n", __func__, sfilepath);
                error = wasm_loader_open_vnode(l, sfilepath, &sub_vp);
                if (error != 0) {
                    printf("%s got error = %d from wasm_loader_open_vnode()\n",__func__, error);
                }
            }
            if (error != 0) {
                sub_dep++;
                continue;
            }

            // FIXME: this does it in a recursive fashion, might need to reduce the nr of recursive calls by returning to top-level to load each sub-module 
            error = wasm_loader_dynld_load_module(l, dlctx, sub_vp, sfilepath, NULL, sub_dep);
            if (error != 0) {
                printf("%s got error = %d from wasm_loader_dynld_load_module()\n",__func__, error);
            }
        }

        sub_dep++;
    }

    return (0);
}

static uintptr_t
alignUp(uintptr_t addr, uint32_t align, uint32_t *pad)
{
    uintptr_t rem;
    if (addr != 0 && align != 0) {
        rem = addr % align;
        if (rem != 0) {
            rem = align - rem;
            addr += rem;
            if (pad) {
                *pad += rem;
            }
        }
    }

    return addr;
}

#define R_WASM_TABLE_INDEX_SLEB 0x01
#define R_WASM_TABLE_INDEX_I32  0x02
#define R_WASM_MEMORY_ADDR_LEB  0x03
#define R_WASM_MEMORY_ADDR_SLEB 0x04
#define R_WASM_MEMORY_ADDR_I32  0x05

struct reloc_leb {
    uint32_t addr;
    char value[5];
} __packed;

struct reloc_i32 {
    uint32_t addr;
    uint32_t value;
} __packed;

int
wasm_loader_internal_reloc_on_module(struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module, char *buf, uint32_t bufsz)
{
    struct mm_arena *dl_arena;
    struct wasm_loader_module_dylink_state *dl_state;
    struct wasm_loader_dylink_section *section;
    struct wa_data_segment_info *data_segments;
    struct wa_elem_segment_info *elem_segments;
    struct wa_data_segment_info *seg;
    struct wa_section_info *code_section;
    char *name;
    uint32_t namesz, segidx, flags, max_align, size;
    uint8_t kind;
    bool more_data, need_data;
    int error;
    uint32_t elem_count, data_count;
    uint32_t count, max_count, lebsz;
    uint32_t roff, rlen, rend, slen;
    uint32_t srcoff;
    uint32_t execfd;
    uint32_t pgcnt;
    uint32_t firstchunk;
    uint32_t chunkoff;
    uint32_t maxchunksz;
    uint32_t *chunksizes;
    struct reloc_leb *leb_vec;
    struct reloc_leb *leb_start;
    struct reloc_leb *leb_end;
    struct reloc_i32 *i32_vec;
    struct reloc_i32 *i32_start;
    struct reloc_i32 *i32_end;
    uint32_t rloc_cnt;
    uint8_t *ptr, *end, *ptr_start;
    void *pg;
    const char *errstr;
    union {
        struct wasm_loader_cmd_rdbuf rdbuf;
        struct wasm_loader_cmd_rloc_i32 rloc_i32;
        struct wasm_loader_cmd_rloc_leb rloc_leb;
    } exec_cmd;

    // find section
    code_section = NULL;
    execfd = module->wa_module_desc_id;
    dl_state = module->wa_dylink_data;
    section = wasm_dylink0_find_subsection(dl_state, NBDL_SUBSEC_RLOCINT);
    if (section == NULL) {
        return EINVAL;
    }

    dl_arena = dlctx->dl_arena;

    roff = section->src_offset;
    slen = section->size;
    rend = roff + slen;
    ptr = (uint8_t *)buf;
    end = (uint8_t *)buf + bufsz;
    ptr_start = ptr;
    maxchunksz = 0;
    errstr = NULL;
    leb_vec = (struct reloc_leb *)buf;
    leb_start = leb_vec;
    rloc_cnt = (bufsz / sizeof(struct reloc_leb));
    leb_end = leb_vec + rloc_cnt;
    //printf("%s leb reloc-count = %d for bufsz = %d leb_start = %p leb_end %p\n", __func__, rloc_cnt, bufsz, leb_start, leb_end);

    i32_vec = (struct reloc_i32 *)buf;
    i32_start = i32_vec;
    rloc_cnt = (bufsz / sizeof(struct reloc_i32));
    i32_end = i32_vec + rloc_cnt;
    //printf("%s leb reloc-count = %d for bufsz = %d i32_start = %p i32_end = %p\n", __func__, rloc_cnt, bufsz, i32_start, i32_end);
    rloc_cnt = 0;


    rlen = MIN(slen, bufsz);
    if (rlen == slen) {
        more_data = false;
    }

    // TODO: this loading must adopt to reading back chunks..

    elem_segments = module->wa_elem_segments;
    elem_count = module->wa_elem_count;

    data_segments = module->wa_data_segments;
    data_count = module->wa_data_count;
    
    exec_cmd.rdbuf.buffer = execfd;
    exec_cmd.rdbuf.dst = buf;
    exec_cmd.rdbuf.src = roff;
    exec_cmd.rdbuf.size = 64;
    error = wasm_exec_ioctl(EXEC_IOCTL_RDBUF, &exec_cmd);

    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;
    firstchunk = roff + (ptr - ptr_start);

    chunksizes = mm_arena_alloc_simple(dl_arena, count * sizeof(uint32_t), NULL);
    if (chunksizes == NULL) {
        printf("%s failed to alloc size-cache for count = %d\n", __func__, count);
        return ENOMEM;
    }

    // first check what the largest symsize (internal relocs are to complex to read page by page)
    for (int i = 0; i < count; i++) {
        uint32_t chunksz, totcnksz, dst_idx, src_type, src_idx, rloc_count, dst_off, dst_base, src_off, src_base;
        uint8_t rloctype;
        rloctype = *(ptr);
        ptr++;
        chunksz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        totcnksz = chunksz + lebsz + 1;
        if (totcnksz > maxchunksz) {
            maxchunksz = totcnksz;
        }

        chunksizes[i] = totcnksz;

        // read next chunk header
        roff += (ptr - ptr_start) + chunksz;
        exec_cmd.rdbuf.buffer = execfd;
        exec_cmd.rdbuf.dst = buf;
        exec_cmd.rdbuf.src = roff;
        exec_cmd.rdbuf.size = 64;
        error = wasm_exec_ioctl(EXEC_IOCTL_RDBUF, &exec_cmd);
        ptr = ptr_start;
    }

    printf("%s max-chunk-size = %d module = %s\n", __func__, maxchunksz, module->wa_module_name);

    pgcnt = howmany(maxchunksz + 32, PAGE_SIZE);
    pg = kmem_page_alloc(pgcnt, 0);
    if (pg == NULL) {
        printf("%s ERROR failed to alloc temp pages (%d) for reloc\n", __func__, pgcnt);
        return ENOMEM;
    }

    roff = firstchunk;
    ptr = pg;
    ptr_start = ptr;

    for(int i = 0; i < count; i++) {
        uint32_t chunksz, dst_idx, src_type, src_idx, rloc_count, dst_off, dst_base, dst_max, dst_addr, src_off, src_base;
        uint8_t rloctype;

        // read the whole symbol chunk.
        exec_cmd.rdbuf.buffer = execfd;
        exec_cmd.rdbuf.dst = pg;
        exec_cmd.rdbuf.src = roff;
        exec_cmd.rdbuf.size = chunksizes[i];
        error = wasm_exec_ioctl(EXEC_IOCTL_RDBUF, &exec_cmd);
        ptr = ptr_start;
        end = ptr_start + exec_cmd.rdbuf.size;

        rloctype = *(ptr);
        ptr++;
        chunksz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        chunkoff = roff + (ptr - ptr_start);
        // only R_WASM_MEMORY_ADDR_I32 has dst_idx
        if (rloctype == R_WASM_MEMORY_ADDR_I32 || rloctype == R_WASM_TABLE_INDEX_I32) {
            dst_idx = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            if (dst_idx >= data_count) {
                //printf("%s dst_idx %d to large\n", __func__, dst_idx);
            }
            dst_base = data_segments[dst_idx].wa_src_offset;
            dst_max = (dst_base + data_segments[dst_idx].wa_size) - 4;
            //printf("%s dst_base = %d of rloctype = %d\n", __func__, dst_base, rloctype);

            src_type = *(ptr);
            ptr++;
            src_idx = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            if (src_type == 1) { // data-segment
                if (src_idx >= data_count) {
                    printf("%s src_idx %d to large\n", __func__, src_idx);
                    error = EINVAL;
                    goto errout;
                }
                src_base = data_segments[src_idx].wa_dst_offset;
            } else if (src_type == 2) { // elem-segment
                if (src_idx >= elem_count) {
                    printf("%s src_idx %d to large\n", __func__, src_idx);
                    error = EINVAL;
                    goto errout;
                }
                src_base = elem_segments[src_idx].wa_dst_offset;
            } else {
                printf("%s INVALID_SRC_TYPE = %d\n", __func__, src_type);
                error = EINVAL;
                goto errout;
            }

            rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            //printf("%s src_base = %d of src_type = %d rloc_count = %d\n", __func__, src_base, src_type, rloc_count);

            for (int x = 0; x < rloc_count; x++) {
                dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                dst_addr = dst_base + dst_off;                
                if (dst_addr < dst_base || dst_addr > dst_max) {
                    printf("%s ERROR i32_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d) segment = %s\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base, data_segments[dst_idx].wa_name);
                }
                i32_vec->addr = dst_addr;
                i32_vec->value = src_base + src_off;
                rloc_cnt++;
                i32_vec++;
                if (i32_vec >= i32_end) {
                    exec_cmd.rloc_i32.buffer = execfd;
                    exec_cmd.rloc_i32.count = rloc_cnt;
                    exec_cmd.rloc_i32.packed_arr = i32_start;
                    error = wasm_exec_ioctl(EXEC_IOCTL_RLOC_I32, &exec_cmd);
                    rloc_cnt = 0;
                    i32_vec = i32_start;
                }
            }

            if (rloc_cnt != 0) {
                exec_cmd.rloc_i32.buffer = execfd;
                exec_cmd.rloc_i32.count = rloc_cnt;
                exec_cmd.rloc_i32.packed_arr = i32_start;
                error = wasm_exec_ioctl(EXEC_IOCTL_RLOC_I32, &exec_cmd);
                rloc_cnt = 0;
                i32_vec = i32_start;             
            }

        } else {
            src_type = *(ptr);
            ptr++;
            src_idx = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            if (code_section == NULL) {
                code_section = wasm_find_section(module, WASM_SECTION_CODE, NULL);
                if (code_section == NULL) {
                    printf("%s ERROR could not find code-section for code-reloc\n", __func__);
                    error = ENOENT;
                    goto errout;
                }
            }

            dst_base = code_section->wa_offset;
            dst_max = (dst_base + code_section->wa_size) - 5;

            if (src_type == 1) { // data-segment
                if (src_idx >= data_count) {
                    printf("%s ERROR src_idx %d to large\n", __func__, src_idx);
                    error = EINVAL;
                    goto errout;
                }
                src_base = data_segments[src_idx].wa_dst_offset;
                //printf("%s src_base = %d of type = %d\n", __func__, src_base, src_type);
            } else if (src_type == 2) { // elem-segment
                if (src_idx >= elem_count) {
                    //printf("%s ERROR src_idx %d to large\n", __func__, src_idx);
                    error = EINVAL;
                    goto errout;
                }
                src_base = elem_segments[src_idx].wa_dst_offset;
                //printf("%s src_base = %d of type = %d\n", __func__, src_base, src_type);
            } else {
                //printf("%s INVALID_SRC_TYPE = %d\n", __func__, src_type);
                error = EINVAL;
                goto errout;
            }

            rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            if (rloctype == R_WASM_MEMORY_ADDR_LEB) {

                for (int x = 0; x < rloc_count; x++) {
                    dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    dst_addr = dst_base + dst_off;
                    if (dst_addr <= dst_base || dst_addr > dst_max) {
                        printf("%s ERROR leb_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d)\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base);
                    }
                    leb_vec->addr = dst_addr;
                    encodeULEB128(src_base + src_off, (uint8_t *)leb_vec->value, 5);
                    rloc_cnt++;
                    leb_vec++;
                    if (leb_vec >= leb_end) {
                        exec_cmd.rloc_leb.buffer = execfd;
                        exec_cmd.rloc_leb.count = rloc_cnt;
                        exec_cmd.rloc_leb.lebsz = 5;
                        exec_cmd.rloc_leb.packed_arr = leb_start;
                        error = wasm_exec_ioctl(EXEC_IOCTL_RLOC_LEB, &exec_cmd);
                        rloc_cnt = 0;
                        leb_vec = leb_start;
                    }
                }

            } else if (rloctype == R_WASM_MEMORY_ADDR_SLEB || rloctype == R_WASM_TABLE_INDEX_SLEB) {

                for (int x = 0; x < rloc_count; x++) {
                    dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    dst_addr = dst_base + dst_off;
                    if (dst_addr < dst_base || dst_addr > dst_max) {
                        printf("%s ERROR leb_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d)\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base);
                    }
                    leb_vec->addr = dst_addr;
                    encodeSLEB128(src_base + src_off, (uint8_t *)leb_vec->value, 5);
                    rloc_cnt++;
                    leb_vec++;
                    if (leb_vec >= leb_end) {
                        exec_cmd.rloc_leb.buffer = execfd;
                        exec_cmd.rloc_leb.count = rloc_cnt;
                        exec_cmd.rloc_leb.lebsz = 5;
                        exec_cmd.rloc_leb.packed_arr = leb_start;
                        error = wasm_exec_ioctl(EXEC_IOCTL_RLOC_LEB, &exec_cmd);
                        rloc_cnt = 0;
                        leb_vec = leb_start;
                    }
                }
            }

            if (rloc_cnt != 0) {
                exec_cmd.rloc_leb.buffer = execfd;
                exec_cmd.rloc_leb.count = rloc_cnt;
                exec_cmd.rloc_leb.lebsz = 5;
                exec_cmd.rloc_leb.packed_arr = leb_start;
                error = wasm_exec_ioctl(EXEC_IOCTL_RLOC_LEB, &exec_cmd);
                rloc_cnt = 0;
                leb_vec = leb_start;             
            }
        }

        roff = chunkoff + chunksz;
    }

    if (pg != NULL)
        kmem_page_free(pg, pgcnt);
    if (chunksizes != NULL)
        mm_arena_free_simple(dl_arena, chunksizes);

    dbg_loading("%s did all relocs\n", __func__);

    return (0);

errout: 

    if (pg != NULL)
        kmem_page_free(pg, pgcnt);
    if (chunksizes != NULL)
        mm_arena_free_simple(dl_arena, chunksizes);

    printf("%s ERROR failed to complete relocs\n", __func__);

    return error;

}

struct wa_data_segment_info *
wasm_rtld_find_data_segment(struct wa_module_info *module, const char *name)
{
    struct wa_data_segment_info *segment;
    uint32_t namesz;
    uint32_t count;

    if (module == NULL || name == NULL)
        return NULL;

    count = module->wa_data_count;
    if (count == 0)
        return NULL;

    namesz = strlen(name);
    segment = module->wa_data_segments;
    for (int i = 0; i < count; i++) {
        if (segment->wa_namesz == namesz && strncmp(name, segment->wa_name, namesz) == 0) {
            return segment;
        }
        segment++;
    }

    return NULL;
}

int
wasm_loader_dynld_do_internal_reloc(struct lwp *l, struct wasm_loader_dl_ctx *dlctx, char *buf, uint32_t bufsz)
{
    struct wa_module_info **modules;
    struct wa_module_info *module;
    struct wa_data_segment_info *data_seg;
    struct wa_elem_segment_info *elem_seg;
    uint32_t wapgs;
    uint32_t count;
    uint32_t segcount;
    int error;
    union {
        struct wasm_loader_cmd_rdbuf rdbuf;
        struct wasm_loader_cmd_cp_buf_to_umem cp_buf;
        struct wasm_loader_cmd_cp_kmem_to_umem cp_kmem;
        struct wasm_loader_cmd_mk_umem mkmem;
        struct wasm_loader_cmd_umem_grow mgrow;
        struct wasm_loader_cmd_umem_grow tblgrow;
        struct wasm_loader_cmd_mk_table mktbl;
        struct wasm_loader_cmd_rloc_leb rloc_leb;
        struct wasm_loader_cmd_rloc_i32 rloc_i32;
    } exec_cmd;

    uint32_t mem_start = dlctx->dl_membase; // TODO: use option but have default at 4096
    uint32_t mem_off = mem_start;
    uint32_t mem_pad = 0;
    uint32_t tbl_start = dlctx->dl_tblbase;
    uint32_t tbl_off = tbl_start;
    uint32_t tbl_pad = 0;
    uintptr_t ps_addr;
    uint32_t strtbl_rt_size, strtbl_rt_off;
    uint32_t module_rt_size, module_rt_off;
    uint32_t module_mem_rt_size, module_mem_rt_off, module_mem_rt_cnt;
    uint32_t modvec_rt_size, modvec_rt_off;
    uint32_t memdesc_rt_size, memdesc_rt_off; 
    struct wasm_module_rt *rtld_module_src;
    struct rtld_segment data_segment_src;
    uintptr_t *modvec_src;
    bool memdesc_used;

    // compute initial data-segments
    // all .rodata are appended after each other
    // all .bss are appended after each other
    module_mem_rt_cnt = 0;
    memdesc_used = false;
    modules = dlctx->dl_modules;
    count = dlctx->dl_module_count;
    for (int i = 0; i < count; i++) {
        module = modules[i];
        segcount = module->wa_data_count;
        data_seg = module->wa_data_segments;
        module_mem_rt_cnt += segcount;
        printf("%s module-name = %s address = %p\n", __func__, module->wa_module_name, module);
        for (int x = 0; x < segcount; x++) {
            if (data_seg->wa_name != NULL && strncmp(data_seg->wa_name, ".rodata", 7) == 0) {
                mem_off = alignUp(mem_off, data_seg->wa_align, &mem_pad);
                data_seg->wa_dst_offset = mem_off;
                mem_off += data_seg->wa_size;
                //printf("%s placing %s %s at %d\n", __func__, module->wa_module_name, data_seg->wa_name, data_seg->wa_dst_offset);
            }
            // finding .dynsym section for later use.
            if (module->wa_dylink_data != NULL && data_seg->wa_name != NULL && strncmp(data_seg->wa_name, ".dynsym", 7) == 0) {
                module->wa_dylink_data->dl_dlsym = data_seg;
            }

            data_seg++;
        }
    }

    // TODO: round-up to page aligned addr

    // skips .rodata & .bss & data seg where dst_offset has been set already
    for (int i = 0; i < count; i++) {
        module = modules[i];
        segcount = module->wa_data_count;
        data_seg = module->wa_data_segments;
        for (int x = 0; x < segcount; x++) {
            if (data_seg->wa_dst_offset != 0 || (data_seg->wa_name != NULL && (strncmp(data_seg->wa_name, ".rodata", 7) == 0 || strncmp(data_seg->wa_name, ".bss", 4) == 0))) {
                // do nothing
            } else {
                mem_off = alignUp(mem_off, data_seg->wa_align, &mem_pad);
                data_seg->wa_dst_offset = mem_off;
                mem_off += data_seg->wa_size;
                dbg_loading("%s placing %s %s at %d\n", __func__, module->wa_module_name, data_seg->wa_name, data_seg->wa_dst_offset);
            }
            data_seg++;
        }
    }

    // TODO: round-up to page aligned addr

    // placing .bss segments
    for (int i = 0; i < count; i++) {
        module = modules[i];
        segcount = module->wa_data_count;
        data_seg = module->wa_data_segments;
        for (int x = 0; x < segcount; x++) {
            if (data_seg->wa_name != NULL && strncmp(data_seg->wa_name, ".bss", 4) == 0) {
                mem_off = alignUp(mem_off, data_seg->wa_align, &mem_pad);
                data_seg->wa_dst_offset = mem_off;
                mem_off += data_seg->wa_size;
                dbg_loading("%s placing %s %s at %d\n", __func__, module->wa_module_name, data_seg->wa_name, data_seg->wa_dst_offset);
            }
            data_seg++;
        }
    }

    // 
    mem_off = alignUp(mem_off, sizeof(void *), &mem_pad);
    ps_addr = mem_off;
    mem_off += sizeof(struct ps_strings);
    dlctx->libc_ps = (void *)ps_addr;

    // computing needed memory for rtld string-table (filepath, name, vers)
    strtbl_rt_size = 0;
    for (int i = 0; i < count; i++) {
        module = modules[i];

        if (module->wa_module_name != NULL) {
            strtbl_rt_size += strlen(module->wa_module_name) + 1;
        }

        if (module->wa_module_vers != NULL) {
            strtbl_rt_size += strlen(module->wa_module_vers) + 1;
        }

        if (module->wa_filepath != NULL) {
            strtbl_rt_size += strlen(module->wa_filepath) + 1;
        }
    }

    mem_off = alignUp(mem_off, sizeof(void *), &mem_pad);
    strtbl_rt_off = mem_off;
    mem_off += strtbl_rt_size;

    // computing needed memory for all struct wasm_module_rt
    // used in dynamic-library info struct for dlsym/dlinfo/dlopen etc.
    module_rt_size = sizeof(struct wasm_module_rt) * count;
    mem_off = alignUp(mem_off, 8, &mem_pad);
    module_rt_off = mem_off;
    mem_off += module_rt_size;
    // to keep all rtld_segment in runtime memory
    module_mem_rt_size = sizeof(struct rtld_segment) * module_mem_rt_cnt;
    mem_off = alignUp(mem_off, 8, &mem_pad);
    module_mem_rt_off = mem_off;
    mem_off += module_mem_rt_size;
    modvec_rt_size = sizeof(void *) * count;
    modvec_rt_size = alignUp(modvec_rt_size, 4, NULL);
    modvec_rt_off = mem_off;
    mem_off += modvec_rt_size;
    dlctx->__rtld_modvec = (uintptr_t)modvec_rt_off;
    
    memdesc_rt_size = sizeof(struct rtld_memory_descriptor);
    mem_off = alignUp(mem_off, 8, &mem_pad);
    memdesc_rt_off = mem_off;
    mem_off += memdesc_rt_size;

    // compute initial element-segments
    // all objc_indirect_functions are appended after each other
    for (int i = 0; i < count; i++) {
        module = modules[i];
        segcount = module->wa_elem_count;
        elem_seg = module->wa_elem_segments;
        for (int x = 0; x < segcount; x++) {
            if (elem_seg->wa_name != NULL && (strncmp(elem_seg->wa_name, "objc_indirect_functions", 23) == 0 || strncmp(elem_seg->wa_name, ".objc_indirect_funcs", 20) == 0)) {
                // do nothing
            } else {
                tbl_off = alignUp(tbl_off, elem_seg->wa_align, &tbl_pad);
                elem_seg->wa_dst_offset = tbl_off;
                tbl_off += elem_seg->wa_size;
                dbg_loading("%s placing %s %s at %d\n", __func__, module->wa_module_name, elem_seg->wa_name, elem_seg->wa_dst_offset);
            }
            elem_seg++;
        }
    }

    // placing all objc_indirect_functions & .objc_indirect_funcs element segments
    for (int i = 0; i < count; i++) {
        module = modules[i];
        segcount = module->wa_elem_count;
        elem_seg = module->wa_elem_segments;
        for (int x = 0; x < segcount; x++) {
            if (elem_seg->wa_name != NULL && (strncmp(elem_seg->wa_name, "objc_indirect_functions", 23) == 0 || strncmp(elem_seg->wa_name, ".objc_indirect_funcs", 20) == 0)) {
                tbl_off = alignUp(tbl_off, elem_seg->wa_align, &tbl_pad);
                elem_seg->wa_dst_offset = tbl_off;
                tbl_off += elem_seg->wa_size;
                dbg_loading("%s placing %s %s at %d\n", __func__, module->wa_module_name, elem_seg->wa_name, elem_seg->wa_dst_offset);
            }
            elem_seg++;
        }
    }

    if (dlctx->dl_mem_created == false) {
        // create env.__linear_memory
        exec_cmd.mkmem.min = 10;
        exec_cmd.mkmem.max = 4096;      // FIXME: take from wa_mod->meminfo
        exec_cmd.mkmem.shared = true;
        exec_cmd.mkmem.bits = 32;
        error = wasm_exec_ioctl(EXEC_IOCTL_MAKE_UMEM, &exec_cmd);
        if (error != 0) {
            printf("%s got error = %d after growing memory with delta = %d (WASM_PAGE_SIZE)\n", __func__, error, wapgs);
        }
        dlctx->dl_mem_created = true;
    }

    // grow env.__linear_memory
    wapgs = howmany(mem_off, WASM_PAGE_SIZE);
    exec_cmd.mgrow.grow_size = wapgs;
    exec_cmd.mgrow.grow_ret = 0;
    error = wasm_exec_ioctl(EXEC_IOCTL_UMEM_GROW, &exec_cmd);
    if (error != 0) {
        printf("%s got error = %d after growing memory with delta = %d (WASM_PAGE_SIZE)\n", __func__, error, wapgs);
    }

    if (dlctx->dl_tbl_created == false) {
        // create env.__indirect_table
        exec_cmd.mktbl.min = tbl_start;
        exec_cmd.mktbl.max = -1;
        exec_cmd.mktbl.reftype = 0x70;
        error = wasm_exec_ioctl(EXEC_IOCTL_UTBL_MAKE, &exec_cmd);
        if (error != 0) {
            printf("%s got error = %d after creating table with desc {min: %d max: %d reftype: %d}\n", __func__, error, exec_cmd.mktbl.min, exec_cmd.mktbl.max, exec_cmd.mktbl.reftype);
        }
        dlctx->dl_tbl_created = true;
    }

    // grow env.__indirect_table
    exec_cmd.tblgrow.grow_size = tbl_off - tbl_start;
    exec_cmd.tblgrow.grow_ret = 0;
    error = wasm_exec_ioctl(EXEC_IOCTL_UTBL_GROW, &exec_cmd);
    if (error != 0) {
        printf("%s got error = %d after growing table with delta = %d", __func__, error, exec_cmd.tblgrow.grow_size);
    }

    // do internal code/data relocs
    for (int i = 0; i < count; i++) {
        module = modules[i];
        error = wasm_loader_internal_reloc_on_module(dlctx, module, buf, bufsz);
        if (error != 0) {
            printf("%s got error = %d from reloc on module\n", __func__, error);
        }
    }

    dlctx->dl_membase = mem_off;
    dlctx->dl_tblbase = tbl_off;

    // copy data-segment into memory
    for (int i = 0; i < count; i++) {
        module = modules[i];
        segcount = module->wa_data_count;
        data_seg = module->wa_data_segments;
        for (int x = 0; x < segcount; x++) {
            // skip non exported data-segments (such as .bss)
            if ((data_seg->wa_flags & (_RTLD_SEGMENT_NOT_EXPORTED|_RTLD_SEGMENT_ZERO_FILL)) != 0) {
                data_seg++;
                continue;
            }

            if (data_seg->wa_dst_offset != 0) {
                
                exec_cmd.cp_buf.buffer = module->wa_module_desc_id;
                exec_cmd.cp_buf.dst_offset = data_seg->wa_dst_offset;
                exec_cmd.cp_buf.src_offset = data_seg->wa_src_offset;
                exec_cmd.cp_buf.size = data_seg->wa_size;
                error = wasm_exec_ioctl(EXEC_IOCTL_CP_BUF_TO_MEM, &exec_cmd);

            } else {
                printf("%s ERROR %s %s missing wa_dst_offset\n", __func__, module->wa_module_name, data_seg->wa_name);
            }
            data_seg++;
        }
    }



    // after relocation we need to update offset for __start if provided
    dlctx->dl_start = 0;
    count = dlctx->dl_module_count;
    for (int i = 0; i < count; i++) {
        struct wash_exechdr_rt *hdr;
        module = modules[i];
        hdr = module->wa_exechdr;
        if (hdr == NULL || hdr->exec_start_elemidx == -1 || hdr->exec_start_funcidx == -1)
            continue;
        
        segcount = module->wa_elem_count;
        elem_seg = module->wa_elem_segments;
        if (hdr->exec_start_elemidx < 0 || hdr->exec_start_elemidx >= segcount) {
            hdr->exec_start_elemidx = -1;
            continue;
        }

        if (hdr->exec_start_elemidx > 0)
            elem_seg += hdr->exec_start_elemidx;

        if (hdr->exec_start_funcidx < 0 || hdr->exec_start_funcidx >= elem_seg->wa_size) {
            hdr->exec_start_funcidx = -1;
            continue;
        }

        hdr->exec_start_funcidx = elem_seg->wa_dst_offset + hdr->exec_start_funcidx;
        printf("%s hdr->exec_start_funcidx = %d\n", __func__, hdr->exec_start_funcidx);

        if (hdr->exec_start_funcidx != -1 && dlctx->dl_start == 0) {
            dlctx->dl_start = (void *)(hdr->exec_start_funcidx);
        }
    }

    // first copyout the addresses that will be the rtld.objlist and rtld.objtail
    dlctx->rtld.objlist = (void *)(module_rt_off);
    dlctx->rtld.objtail = (void *)(module_rt_off + (sizeof(struct wasm_module_rt) * (count - 1)));
    dlctx->rtld.objcount = count;

    // creating and copying all module_rt data
    rtld_module_src = (struct wasm_module_rt *)buf;
    memset(&data_segment_src, 0, sizeof(data_segment_src));

    for (int i = 0; i < count; i++) {
        struct wash_exechdr_rt *hdr;
        struct wa_data_segment_info *segment;
        uint32_t strsz;
        module = modules[i];
        wasm_memory_fill(rtld_module_src, 0, sizeof(struct wasm_module_rt));
        hdr = module->wa_exechdr;
        if (hdr != NULL) {
            rtld_module_src->__start = (void (*)(void (*)(void), struct ps_strings *))(hdr->exec_start_funcidx != -1 ? hdr->exec_start_funcidx : 0);
        }

        if (module->wa_dylink_data && module->wa_dylink_data->dl_dlsym) {
            segment = module->wa_dylink_data->dl_dlsym;
            rtld_module_src->dlsym_start = (void *)segment->wa_dst_offset;
            rtld_module_src->dlsym_end = (void *)(segment->wa_dst_offset + segment->wa_size);
        }

        segment = wasm_rtld_find_data_segment(module, ".init_array");
        if (segment) {
            rtld_module_src->init_array = (void *)segment->wa_dst_offset;
            rtld_module_src->init_array_count = (segment->wa_size / sizeof(void *));
        }

        segment = wasm_rtld_find_data_segment(module, ".fnit_array");
        if (segment) {
            rtld_module_src->fnit_array = (void *)segment->wa_dst_offset;
            rtld_module_src->fnit_array_count = (segment->wa_size / sizeof(void *));
        }

        // setting pointer to next wasm_module_rt struct, in other cases its already assigned to NULL
        if (i != count - 1) {
            rtld_module_src->next = (void *)(module_rt_off + sizeof(struct wasm_module_rt));
        }

        rtld_module_src->ld_dev = module->wa_file_dev;
        rtld_module_src->ld_ino = module->wa_file_ino;

        if (module->wa_module_name != NULL) {
            strsz = strlen(module->wa_module_name);
            copyout(module->wa_module_name, (void *)strtbl_rt_off, strsz + 1);
            rtld_module_src->dso_name = (void *)strtbl_rt_off;
            rtld_module_src->dso_namesz = strsz;
            strtbl_rt_off += (strsz + 1);
        }

        if (module->wa_module_vers != NULL) {
            strsz = strlen(module->wa_module_vers);
            copyout(module->wa_module_vers, (void *)strtbl_rt_off, strsz + 1);
            rtld_module_src->dso_vers = (void *)strtbl_rt_off;
            rtld_module_src->dso_verssz = strsz;
            strtbl_rt_off += (strsz + 1);
        }

        if (module->wa_filepath != NULL) {
            strsz = strlen(module->wa_filepath);
            copyout(module->wa_filepath, (void *)strtbl_rt_off, strsz + 1);
            rtld_module_src->filepath = (void *)strtbl_rt_off;
            strtbl_rt_off += (strsz + 1);
        }

        if (module->memdesc && memdesc_used == false) {
            copyout(module->memdesc, (void *)memdesc_rt_off, sizeof(struct rtld_memory_descriptor));
            rtld_module_src->memdesc = (void *)memdesc_rt_off;
            memdesc_used = true;
        }

        rtld_module_src->data_segments_count = module->wa_data_count;
        rtld_module_src->data_segments = (void *)module_mem_rt_off;

        int data_count = module->wa_data_count;
        segment = module->wa_data_segments;
        for (int x = 0; x < data_count; x++) {
            data_segment_src.addr = segment->wa_dst_offset;
            data_segment_src.size = segment->wa_size;
            data_segment_src.align = segment->wa_align;
            // TODO: copy name?? (not required since rtld are to be used for the loading instead..)

            exec_cmd.cp_kmem.buffer = module->wa_module_desc_id;
            exec_cmd.cp_kmem.dst_offset = module_mem_rt_off;
            exec_cmd.cp_kmem.src = &data_segment_src;
            exec_cmd.cp_kmem.size = sizeof(struct rtld_segment);
            error = wasm_exec_ioctl(EXEC_IOCTL_CP_KMEM_TO_UMEM, &exec_cmd);
            module_mem_rt_off += sizeof(struct rtld_segment);
            segment++;
        } 

        exec_cmd.cp_kmem.buffer = module->wa_module_desc_id;
        exec_cmd.cp_kmem.dst_offset = module_rt_off;
        exec_cmd.cp_kmem.src = rtld_module_src;
        exec_cmd.cp_kmem.size = sizeof(struct wasm_module_rt);
        error = wasm_exec_ioctl(EXEC_IOCTL_CP_KMEM_TO_UMEM, &exec_cmd);
        if (error != 0) {
            printf("%s got error = %d from cpy kmem to umem\n", __func__, error);
        }

        module->wa_dylink_data->rtld_modulep = (struct wasm_module_rt *)module_rt_off;
        module_rt_off += sizeof(struct wasm_module_rt);
    }

    // setting rtld.objmain
    if (dlctx->dl_mainobj) {
        dlctx->rtld.objmain = dlctx->dl_mainobj->wa_dylink_data->rtld_modulep;
    }

    modvec_src = (void *)buf;
    for (int i = 0; i < count; i++) {
        struct wash_exechdr_rt *hdr;
        module = modules[i];
        *modvec_src = (uintptr_t)(module->wa_dylink_data->rtld_modulep);
        modvec_src++;
    }

    exec_cmd.cp_kmem.buffer = -1;
    exec_cmd.cp_kmem.dst_offset = modvec_rt_off;
    exec_cmd.cp_kmem.src = (void *)buf;
    exec_cmd.cp_kmem.size = sizeof(void *) * count;
    error = wasm_exec_ioctl(EXEC_IOCTL_CP_KMEM_TO_UMEM, &exec_cmd);
    if (error != 0) {
        printf("%s got error = %d from cpy kmem to umem\n", __func__, error);
    }

    return (0);
}

int
wasm_loader_call_dlsym()
{
    return (0);
}

int
wasm_loader_extern_reloc_on_module(struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module, char *buf, uint32_t bufsz)
{
    // extern reloc is similar to internal, only that these are weakly linked trough a name which are checked against other modules.
    struct mm_arena *dl_arena;
    struct wasm_loader_module_dylink_state *dl_state;
    struct wasm_loader_dylink_section *section;
    struct wa_data_segment_info *data_segments;
    struct wa_elem_segment_info *elem_segments;
    struct wa_data_segment_info *seg;
    struct wa_section_info *code_section;
    char *name;
    uint32_t namesz, segidx, flags, max_align, size;
    uint32_t maxnamesz;
    uint8_t kind;
    bool more_data, need_data;
    int error;
    uint32_t elem_count, data_count;
    uint32_t count, max_count, lebsz;
    uint32_t roff, rlen, rend, slen, module_count;
    uint32_t srcoff;
    uint32_t execfd;
    uint32_t pgcnt;
    uint32_t firstchunk;
    uint32_t chunkoff;
    uint32_t rloc_count;
    uint32_t maxchunksz;
    uint32_t *chunksizes;
    struct reloc_leb *leb_vec;
    struct reloc_leb *leb_start;
    struct reloc_leb *leb_end;
    struct reloc_i32 *i32_vec;
    struct reloc_i32 *i32_start;
    struct reloc_i32 *i32_end;
    struct wa_module_info **modules;
    struct wa_module_info *submod;
    uint32_t rloc_cnt;
    uint8_t *ptr, *end, *ptr_start;
    uint8_t *chunk_start;
    void *pg;
    char *tmpnamep;
    const char *errstr;
    union {
        struct wasm_loader_cmd_rdbuf rdbuf;
        struct wasm_loader_cmd_rloc_i32 rloc_i32;
        struct wasm_loader_cmd_rloc_leb rloc_leb;
        struct wasm_loader_dynld_dlsym dlsym;
    } exec_cmd;

    // find section
    code_section = NULL;
    execfd = module->wa_module_desc_id;
    dl_state = module->wa_dylink_data;
    section = wasm_dylink0_find_subsection(dl_state, NBDL_SUBSEC_RLOCEXT);
    if (section == NULL) {
        return EINVAL;
    }

    dl_arena = dlctx->dl_arena;

    dbg_loading("%s external reloc on module = %s addr = %p\n", __func__, module->wa_module_name, module);

    roff = section->src_offset;
    slen = section->size;
    rend = roff + slen;
    ptr = (uint8_t *)buf;
    end = (uint8_t *)buf + bufsz;
    ptr_start = ptr;
    maxchunksz = 0;
    maxnamesz = 0;
    errstr = NULL;
    leb_vec = (struct reloc_leb *)dlctx->dl_buf;
    leb_start = leb_vec;
    rloc_cnt = (dlctx->dl_bufsz / sizeof(struct reloc_leb));
    leb_end = leb_vec + rloc_cnt;
    dbg_loading("%s leb reloc-count = %d for bufsz = %d leb_start = %p leb_end %p\n", __func__, rloc_cnt, dlctx->dl_bufsz, leb_start, leb_end);

    i32_vec = (struct reloc_i32 *)dlctx->dl_buf;
    i32_start = i32_vec;
    rloc_cnt = (dlctx->dl_bufsz / sizeof(struct reloc_i32));
    i32_end = i32_vec + rloc_cnt;
    dbg_loading("%s leb reloc-count = %d for bufsz = %d i32_start = %p i32_end = %p\n", __func__, rloc_cnt, dlctx->dl_bufsz, i32_start, i32_end);
    rloc_cnt = 0;


    rlen = MIN(slen, bufsz);
    if (rlen == slen) {
        more_data = false;
    }

    // TODO: this loading must adopt to reading back chunks..

    elem_segments = module->wa_elem_segments;
    elem_count = module->wa_elem_count;

    data_segments = module->wa_data_segments;
    data_count = module->wa_data_count;
    
    exec_cmd.rdbuf.buffer = execfd;
    exec_cmd.rdbuf.dst = buf;
    exec_cmd.rdbuf.src = roff;
    exec_cmd.rdbuf.size = 64;
    error = wasm_exec_ioctl(EXEC_IOCTL_RDBUF, &exec_cmd);

    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;
    firstchunk = roff + (ptr - ptr_start);

    chunksizes = mm_arena_alloc_simple(dl_arena, count * sizeof(uint32_t), NULL);
    if (chunksizes == NULL) {
        printf("%s failed to alloc size-cache for count = %d\n", __func__, count);
        return ENOMEM;
    }

    // first check what the largest symsize (internal relocs are to complex to read page by page)
    for(int i = 0; i < count; i++) {
        uint32_t chunksz, totcnksz, dst_idx, src_type, src_idx, rloc_count, dst_off, dst_base, src_off, src_base;
        uint8_t rloctype;
        rloctype = *(ptr);
        ptr++;
        chunksz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        totcnksz = chunksz + lebsz + 1;
        if (totcnksz > maxchunksz) {
            maxchunksz = totcnksz;
        }
        chunksizes[i] = totcnksz;

        namesz = decodeULEB128(ptr, &lebsz, end, &errstr);  // do not incremnt for size since we want to skip the rest..
        if (namesz > maxnamesz) {
            maxnamesz = namesz;
        }

        //printf("chunk-size = %d name-size = %d\n", chunksz, namesz);
        
        // read next chunk header
        roff += (ptr - ptr_start) + chunksz;
        exec_cmd.rdbuf.buffer = execfd;
        exec_cmd.rdbuf.dst = buf;
        exec_cmd.rdbuf.src = roff;
        exec_cmd.rdbuf.size = 64;
        error = wasm_exec_ioctl(EXEC_IOCTL_RDBUF, &exec_cmd);
        ptr = ptr_start;
    }

    dbg_loading("%s max-chunk-size = %d count %d module = %s\n", __func__, maxchunksz, count, module->wa_module_name);

    pgcnt = howmany(maxchunksz + 32, PAGE_SIZE);
    pg = kmem_page_alloc(pgcnt, 0);
    if (pg == NULL) {
        printf("%s ERROR failed to alloc temp pages (%d) for reloc\n", __func__, pgcnt);
        return ENOMEM;
    }

    tmpnamep = mm_arena_alloc_simple(dl_arena, maxnamesz + 1, 0);

    // 
    module_count = dlctx->dl_module_count;
    modules = dlctx->dl_modules;

    code_section = wasm_find_section(module, WASM_SECTION_CODE, NULL);
    if (code_section == NULL) {
        printf("%s ERROR could not find code-section for code-reloc\n", __func__);
        error = ENOENT;
        goto errout;
    }

    roff = firstchunk;
    ptr = pg;
    ptr_start = ptr;
    end = pg + (pgcnt * PAGE_SIZE);

    for(int i = 0; i < count; i++) {
        uint32_t chunksz, dst_idx, src_type, src_idx, rloc_count, dst_off, dst_base, dst_max, dst_addr, src_off, src_base;
        uint8_t symtype;
        bool found;

        // read the whole symbol chunk.
        exec_cmd.rdbuf.buffer = execfd;
        exec_cmd.rdbuf.dst = ptr_start;
        exec_cmd.rdbuf.src = roff;
        exec_cmd.rdbuf.size = chunksizes[i];
        error = wasm_exec_ioctl(EXEC_IOCTL_RDBUF, &exec_cmd);
        ptr = ptr_start;
        end = pg + exec_cmd.rdbuf.size;

        symtype = *(ptr);
        ptr++;
        chunksz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        chunk_start = ptr;
        namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        name = (char *)ptr;
        ptr += namesz;

        if (symtype == 0) {
            symtype = 1;
        } else if (symtype == 1) {
            symtype = 2;
        } else {
            printf("non-supported symtype %d in module %s\n", symtype, module->wa_module_name);
            symtype = 255;
        }

        found = false;

        strlcpy(tmpnamep, name, namesz + 1);

        for (int x = 0; x < module_count; x++) {

            submod = modules[x];
            if (submod == module) {
                continue;
            }

            if (submod->wa_dylink_data == NULL || submod->wa_dylink_data->dl_dlsym == NULL) {
                printf("%s module = %s (%p) missing .dynsym data..\n", __func__, submod->wa_module_name, submod);
                continue;
            }

            struct wa_data_segment_info *dlsym = submod->wa_dylink_data->dl_dlsym;

            if (dlsym == NULL)
                continue;
            
            exec_cmd.dlsym.dynsym_start = (void *)dlsym->wa_dst_offset;
            exec_cmd.dlsym.dynsym_end = (void *)(dlsym->wa_dst_offset + dlsym->wa_size);
            exec_cmd.dlsym.symbol_name = name;
            exec_cmd.dlsym.symbol_size = namesz;
            exec_cmd.dlsym.symbol_type = symtype;
            exec_cmd.dlsym.symbol_addr = 0;
            error = wasm_exec_ioctl(EXEC_IOCTL_DYNLD_DLSYM_EARLY, &exec_cmd);
            if (error != 0) {
                continue;
            }

            found = true;
            src_base = exec_cmd.dlsym.symbol_addr;
            dbg_loading("%s found symbol '%s' on module '%s' at = %p\n", __func__, tmpnamep, submod->wa_module_name, (void *)src_base);
        }

        if (!found) {
            printf("%s symbol '%s' not found (namesz = %d chunksz = %d)\n", __func__, tmpnamep, namesz, chunksizes[i]);
            roff += (chunk_start - ptr_start) + chunksz;
            continue;
        }

        // uleb relocs
        rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (rloc_count > 0) {

            dst_base = code_section->wa_offset;
            dst_max = (dst_base + code_section->wa_size) - 5;

            for (int x = 0; x < rloc_count; x++) {
                dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                dst_addr = dst_base + dst_off;
                if (dst_addr <= dst_base || dst_addr > dst_max) {
                    printf("%s ERROR leb_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d)\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base);
                }
                leb_vec->addr = dst_addr;
                encodeULEB128(src_base + src_off, (uint8_t *)leb_vec->value, 5);
                rloc_cnt++;
                leb_vec++;
                if (leb_vec >= leb_end) {
                    exec_cmd.rloc_leb.buffer = execfd;
                    exec_cmd.rloc_leb.count = rloc_cnt;
                    exec_cmd.rloc_leb.lebsz = 5;
                    exec_cmd.rloc_leb.packed_arr = leb_start;
                    error = wasm_exec_ioctl(EXEC_IOCTL_RLOC_LEB, &exec_cmd);
                    rloc_cnt = 0;
                    leb_vec = leb_start;
                }
            }

            // 
            if (rloc_cnt != 0) {
                exec_cmd.rloc_leb.buffer = execfd;
                exec_cmd.rloc_leb.count = rloc_cnt;
                exec_cmd.rloc_leb.lebsz = 5;
                exec_cmd.rloc_leb.packed_arr = leb_start;
                error = wasm_exec_ioctl(EXEC_IOCTL_RLOC_LEB, &exec_cmd);
                rloc_cnt = 0;
                leb_vec = leb_start;             
            }
        }

        // sleb relocs
        rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (rloc_count > 0) {

            dst_base = code_section->wa_offset;
            dst_max = (dst_base + code_section->wa_size) - 5;

            for (int x = 0; x < rloc_count; x++) {
                dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                dst_addr = dst_base + dst_off;
                if (dst_addr <= dst_base || dst_addr > dst_max) {
                    printf("%s ERROR leb_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d)\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base);
                }
                leb_vec->addr = dst_addr;
                encodeSLEB128(src_base + src_off, (uint8_t *)leb_vec->value, 5);
                rloc_cnt++;
                leb_vec++;
                if (leb_vec >= leb_end) {
                    exec_cmd.rloc_leb.buffer = execfd;
                    exec_cmd.rloc_leb.count = rloc_cnt;
                    exec_cmd.rloc_leb.lebsz = 5;
                    exec_cmd.rloc_leb.packed_arr = leb_start;
                    error = wasm_exec_ioctl(EXEC_IOCTL_RLOC_LEB, &exec_cmd);
                    rloc_cnt = 0;
                    leb_vec = leb_start;
                }
            }

            // 
            if (rloc_cnt != 0) {
                exec_cmd.rloc_leb.buffer = execfd;
                exec_cmd.rloc_leb.count = rloc_cnt;
                exec_cmd.rloc_leb.lebsz = 5;
                exec_cmd.rloc_leb.packed_arr = leb_start;
                error = wasm_exec_ioctl(EXEC_IOCTL_RLOC_LEB, &exec_cmd);
                rloc_cnt = 0;
                leb_vec = leb_start;             
            }
        }

        // data relocs
        rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (rloc_count > 0) {
            
            for (int x = 0; x < rloc_count; x++) {
                dst_idx = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;

                if (dst_idx >= data_count) {
                    printf("%s dst_idx %d to large (in %d of %d, ptr = %p ptr_start %p)\n", __func__, dst_idx, x, rloc_count, ptr, ptr_start);
                }
                dst_base = data_segments[dst_idx].wa_dst_offset;
                dst_max = (dst_base + data_segments[dst_idx].wa_size) - 4;
                dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                dst_addr = dst_base + dst_off;
                if (dst_addr < dst_base || dst_addr > dst_max) {
                    printf("%s ERROR i32_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d) segment = %s\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base, data_segments[dst_idx].wa_name);
                }
                i32_vec->addr = dst_addr;
                i32_vec->value = src_base + src_off;
                rloc_cnt++;
                i32_vec++;
                if (i32_vec >= i32_end) {
                    exec_cmd.rloc_i32.buffer = -1;
                    exec_cmd.rloc_i32.count = rloc_cnt;
                    exec_cmd.rloc_i32.packed_arr = i32_start;
                    error = wasm_exec_ioctl(EXEC_IOCTL_RLOC_I32, &exec_cmd);
                    rloc_cnt = 0;
                    i32_vec = i32_start;
                }
            }

            if (rloc_cnt != 0) {
                exec_cmd.rloc_i32.buffer = -1;
                exec_cmd.rloc_i32.count = rloc_cnt;
                exec_cmd.rloc_i32.packed_arr = i32_start;
                error = wasm_exec_ioctl(EXEC_IOCTL_RLOC_I32, &exec_cmd);
                rloc_cnt = 0;
                i32_vec = i32_start;             
            }
        }

        roff += (chunk_start - ptr_start) + chunksz;
    }

    if (pg != NULL)
        kmem_page_free(pg, pgcnt);
    if (chunksizes != NULL)
        mm_arena_free_simple(dl_arena, chunksizes);
    if (tmpnamep != NULL)
        mm_arena_free_simple(dl_arena, tmpnamep);

    printf("%s did all external relocs\n", __func__);

    return (0);

errout: 

    if (pg != NULL)
        kmem_page_free(pg, pgcnt);
    if (chunksizes != NULL)
        mm_arena_free_simple(dl_arena, chunksizes);
    if (tmpnamep != NULL)
        mm_arena_free_simple(dl_arena, tmpnamep);

    printf("%s failed to complete relocs\n", __func__);

    return error;
}

int
wasm_loader_dynld_do_extern_reloc(struct lwp *l, struct wasm_loader_dl_ctx *dlctx, char *buf, uint32_t bufsz)
{
    struct wa_module_info **modules;
    struct wa_module_info *module;
    uint32_t count;
    int error;

    if (!dlctx->dynld_loaded) {
        error = wasm_loader_load_rtld_module(dlctx);
        if (error != 0)
            printf("%s got error = %d from wasm_loader_load_rtld_module()\n", __func__, error);
        dlctx->dynld_loaded = true;
    }

    // resolve dlsym based symbol-references
    modules = dlctx->dl_modules;
    count = dlctx->dl_module_count;

    for (int i = 1; i < count; i++) {
        module = modules[i];
        error = wasm_loader_extern_reloc_on_module(dlctx, module, buf, bufsz);
        if (error != 0)
            printf("%s got error = %d from wasm_loader_extern_reloc_on_module() %s \n", __func__, error, module->wa_module_name);
    }

    // do external dependant code/data relocs

    //

    return (0);
}

int
wasm_loader_dynld_compile_and_run(struct lwp *l, struct wasm_loader_dl_ctx *dlctx, char *buf, uint32_t bufsz)
{
    struct wa_module_info **modules;
    struct wa_module_info *module;
    struct wa_section_info *sec, **sections;
    struct buf_remap_param *ptr, *ptr_start, *tmp;
    uint32_t count;
    bool include_custom_names;
    int error;
    union {
        struct wasm_loader_cmd_buf_remap remap;
        struct wasm_loader_cmd_compile_v2 run;
    } exec_cmd;

    include_custom_names = true;
    modules = dlctx->dl_modules;
    count = dlctx->dl_module_count;
    ptr_start = (struct buf_remap_param *)buf;

    // clean-up each execution buffer by remapping section (excluding what not needed)
    // data-segments are already handled, so these are excluded in the final binary
    // relocation data is not needed at runtime so that can be excluded as well..
    // custom name section can be made optional.
    for (int i = 0; i < count; i++) {
        uint32_t new_bufsize, remap_cnt, xlen, sectsz;
        bool include;
        module = modules[i];
        ptr = ptr_start;
        sections = module->wa_sections;
        xlen = module->wa_section_count;

        remap_cnt = 0;
        new_bufsize = 0;
        // the first 8-bytes are mandatory
        ptr->dst = 0;
        ptr->src = 0;
        ptr->len = 8;

        for (int x = 0; x < xlen; x++) {
            sec = sections[x];
            include = true;
            if (sec->wa_type == WASM_SECTION_DATA || sec->wa_type == WASM_SECTION_DATA_COUNT) {
                include = false;
            } else if (sec->wa_type == WASM_SECTION_CUSTOM) {
                // 
                if (strncmp(sec->wa_name, "rtld.dylink.0", 13) == 0) {
                    include = false;
                } else if (include_custom_names == false && strncmp(sec->wa_name, "name", 4) == 0) {
                    include = false;
                }
            }

            if (include == true) {
                // there are two scenarios for include:
                //     empty chunk : the section before this one was not included
                // non-empty chunk : the section before was included, simply append size

                // the value in wa_size is from after the uleb itself, to get the total we need to combine.
                sectsz = (sec->wa_offset - sec->wa_sectionStart) + sec->wa_size;
                if (ptr->len == 0) {
                    ptr->src = sec->wa_sectionStart;
                    ptr->len = sectsz;
                } else {
                    ptr->len += sectsz;
                }

            } else if (ptr->len != 0) {
                tmp = ptr;
                ptr++;
                ptr->dst = tmp->dst + tmp->len;
                ptr->src = 0;
                ptr->len = 0;
                new_bufsize += tmp->len;
                remap_cnt++;
            }
        }

        if (ptr->len != 0) {
            new_bufsize += ptr->len;
            remap_cnt++;
            ptr++;
            ptr->dst = 0;
            ptr->src = 0;
            ptr->len = 0;
        }

        exec_cmd.remap.buffer = module->wa_module_desc_id;
        exec_cmd.remap.new_size = new_bufsize;
        exec_cmd.remap.remap_count = remap_cnt;
        exec_cmd.remap.remap_data = remap_cnt == 0 ? NULL : ptr_start;
        error = wasm_exec_ioctl(EXEC_IOCTL_BUF_REMAP, &exec_cmd);
        if (error != 0) {
            printf("%s got error = %d when remapping buffer for module = %s\n", __func__, error, module->wa_module_name);
        }
    }


    // compile and instanciate each module
    for (int i = 0; i < count; i++) {
        module = modules[i];

        // only libobjc2 is exported using regular wasm exports.
        // TODO: but this should realy be a flag within dylink.0
        if (strncmp(module->wa_module_name, "libobjc2", 8) == 0) {
            exec_cmd.run.export_name = module->wa_module_name;
            exec_cmd.run.export_namesz = strlen(module->wa_module_name);
        } else {
            exec_cmd.run.export_name = NULL;
            exec_cmd.run.export_namesz = 0;
        }
        exec_cmd.run.buffer = module->wa_module_desc_id;
        exec_cmd.run.__dso_handle = (uintptr_t)(module->wa_dylink_data ? module->wa_dylink_data->rtld_modulep : 0);  // should point to the module-info struct in user-memory
        exec_cmd.run.__tls_handle = 0;
        exec_cmd.run.errno = 0;
        exec_cmd.run.errphase = 0;
        exec_cmd.run.flags = 0;
        exec_cmd.run.errmsg = buf;
        exec_cmd.run.errmsgsz = bufsz;
        error = wasm_exec_ioctl(EXEC_IOCTL_COMPILE, &exec_cmd);
        if (error != 0) {
            printf("%s got error = %d when compiling module = %s\n", __func__, error, module->wa_module_name);
        }
    }

    // do external dependant code/data relocs

    //

    return (0);
}

int
wasm_loader_sort_modules(struct wasm_loader_dl_ctx *dlctx)
{
    struct wa_module_info **modules;
    struct wa_module_info *module;
    struct wa_module_info *first;
    uint32_t count;

    modules = dlctx->dl_modules;
    count = dlctx->dl_module_count;
    first = modules[0];

    // reverse the sorting (TODO: should be sorting so that dependencies are done before)
    for (int i = 1; i < count; i++) {
        module = modules[i];
        modules[i - 1] = module;
    }

    modules[count - 1] = first;

    return (0);
}

// new approach which does the heavy lifting in rtld

struct ldwasm_execpkg {
    uintptr_t uesp;
    uintptr_t rlocbase;
};

void
ldwasm_exec_trampline(void *arg)
{
    struct ldwasm_execpkg *pkg;

    pkg = arg;
}

int load_rtld_module_from_disk(void);
int setup_ldwasm_module_from_cache(uintptr_t relocbase, int32_t mem_min, int32_t mem_max);

int
load_ldwasm_module(uintptr_t rlocbase, int32_t memory_min, int32_t memory_max)
{
    int error;

    error = load_rtld_module_from_disk();
    if (error)
        return error;

    error = setup_ldwasm_module_from_cache(rlocbase, memory_min, memory_max);

    return error;
}

int
exec_load_ldwasm_binary(struct lwp *l, struct exec_vmcmd *cmd)
{
    struct wash_exechdr_rt *hdr;
    struct wasm_exechdr_secinfo *sec;
    uintptr_t rlocbase;
    uintptr_t uesp, uesp0, stacksz;
    uint32_t min_stacksz;
    int32_t memory_min, memory_max;
    int error;

    min_stacksz = 131072;
    rlocbase = 1024;
    // 1. get the exechdr

    // 2. determine memory.initial & memory.maximum and let that be the base for all linking.
    sec = _rtld_exechdr_find_section(hdr, WASM_SECTION_IMPORT, NULL);
    memory_min = -1;
    memory_max = -1;

    // 3. determine stack-size and alloc that staring at reloc base
    rlocbase = roundup2(rlocbase, PAGE_SIZE);
    stacksz = hdr->stack_size_hint;
    if (stacksz < min_stacksz)
        stacksz = min_stacksz;
    stacksz = roundup2(stacksz, PAGE_SIZE);
    uesp0 = roundup2((rlocbase + stacksz), PAGE_SIZE);
    rlocbase = uesp0;

    //
    error = load_ldwasm_module(rlocbase, memory_min, memory_max);

    // setup trampoline to let rtld do the rest of the job.
    struct switchframe *sf;
    struct pcb *pcb;
    struct ldwasm_execpkg *exec_arg;

    pcb = lwp_getpcb(l);
    sf = (struct switchframe *)pcb->pcb_esp;

    if (sf->sf_ebx == (int)(NULL) || ((struct wasm32_execpkg_args *)sf->sf_ebx)->et_sign != WASM_EXEC_PKG_SIGN) {

        exec_arg = kmem_zalloc(sizeof(struct ldwasm_execpkg), 0);
        exec_arg->rlocbase = rlocbase;
        exec_arg->uesp = uesp;
        
        sf->sf_esi = (int)ldwasm_exec_trampline;
        sf->sf_ebx = (int)exec_arg;
        sf->sf_eip = (int)lwp_trampoline;
        printf("%s setting switchframe at %p for lwp %p\n", __func__, sf, wasm_curlwp);
    }
}

void wasm_exec_trampline(void *);

/**
 * Unlike other vmcmd this one is used to hoist the binary from disk and into 
 * a ArrayBuffer that is within the JavaScript runtime.
 * 
 * TODO: be sure to handle `new WebAssembly.Module` at the correct time, if start section is
 * defined that runs the binary right away.
 * TODO: enforce start to be a forbidden section, (tool to translate start into DT_START)
 * TODO: since everything must be passed trough here anyways, we might as well just process
 * the wasm structure here; parsing section and capturing anything of intrest
 * - memory section or imported memory; could change memory limits if compiled with padding.
 * - custom section; find netbsd custom section to extractruntime specific attributes.
 * TODO: use a malloc arena together with a loading context.
 */
int
exec_load_wasm32_binary(struct lwp *l, struct exec_vmcmd *cmd)
{
    struct vnode *vp = cmd->ev_vp;
    struct vattr vap;
    struct wa_module_info *wa_mod;
    struct wa_section_info *sec;
    struct wasm_loader_args *args;
    struct mm_arena *dl_arena;
    struct wasm_loader_dl_ctx *dlctx;
    struct wasm_loader_module_dylink_state *dl_state;
    struct wash_exechdr_rt *exehdr_rt;
    size_t skiplen;
    char *buf, *filepath;
    uint32_t ver;
    uint32_t count;
    uint32_t off, bufsz, bsize, fsize;
    uint32_t exec_flags = 0;
    uint32_t exec_end_offset = 0;
    uint32_t stack_size_hint = 0;
    uint32_t uesp0;
    int32_t exec_bufid;
    bool is_dyn = false;
    int error;
    union {
        struct wasm_loader_cmd_mkbuf mkbuf;
        struct wasm_loader_cmd_wrbuf wrbuf;
        struct wasm_loader_cmd_rdbuf rdbuf;
        struct wasm_loader_cmd_cp_buf_to_umem cp_buf;
        struct wasm_loader_cmd_cp_buf_to_umem cp_kmem;
        struct wasm_loader_cmd_mk_umem mkmem;
        struct wasm_loader_cmd_umem_grow mgrow;
        struct wasm_loader_cmd_rloc_leb rloc_leb;
        struct wasm_loader_cmd_rloc_i32 rloc_i32;
    } exec_cmd;

    exehdr_rt = NULL;
    dl_arena = mm_arena_create_simple("wasm_dl", NULL);
    dlctx = mm_arena_zalloc_simple(dl_arena, sizeof(struct wasm_loader_dl_ctx), NULL);
    printf("%s dlctx = %p dl_arena = %p\n", __func__, dlctx, dl_arena);
    if (dlctx == NULL) {
        printf("%s did get error %d from mm_arena_alloc(%p)\n", __func__, error, dl_arena);
    }
    dlctx->dl_arena = dl_arena;
    dlctx->dl_membase = 4096;
    dlctx->dl_tblbase = 1;

#if 0
    vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

    error = VOP_GETATTR(vp, &vap, l->l_cred);
    if (error != 0) {
        VOP_UNLOCK(vp);
        printf("%s error when VOP_GETATTR() called %d", __func__, error);
        return error;
    }

    fsize = vap.va_size;
    bsize = vap.va_blocksize;
#endif

    if (cmd->ev_addr != 0) {
        args = (struct wasm_loader_args *)cmd->ev_addr;
        exec_flags = args->exec_flags;
        if (args->ep_exechdr) {
            exehdr_rt = args->ep_exechdr;
            stack_size_hint = exehdr_rt->stack_size_hint;
        }
        exec_end_offset = fsize;
        if (args->ep_resolvedname != NULL) {
            filepath = mm_arena_zalloc_simple(dl_arena, args->ep_resolvednamesz + 1, NULL);
            strlcpy(filepath, args->ep_resolvedname, args->ep_resolvednamesz + 1);
            kmem_free(args->ep_resolvedname, 0);
        }
    } else {
        exec_end_offset = fsize;
        filepath = NULL;
    }

    if (stack_size_hint == 0) {
        stack_size_hint = WASM_PAGE_SIZE * 2;
    }

    printf("stack_size_hint = %d\n", stack_size_hint);

    error = wasm_loader_dynld_load_module(l, dlctx, vp, filepath, exehdr_rt, NULL);
    if (error != 0) {
        printf("%s got error = %d after wasm_loader_dynld_load_module()\n", __func__, error);
    }

    // once we have returned from wasm_loader_dynld_load_module() all dylink modules should be loaded
    printf("%s all dylink modules loaded!\n", __func__);

    wasm_loader_sort_modules(dlctx);

    error = wasm_loader_dynld_do_internal_reloc(l, dlctx, dlctx->dl_buf, dlctx->dl_bufsz);
    if (error != 0) {
        printf("%s got error = %d after wasm_loader_dynld_do_internal_reloc()\n", __func__, error);
    }

    error = wasm_loader_dynld_do_extern_reloc(l, dlctx, dlctx->dl_buf, dlctx->dl_bufsz);
    if (error != 0) {
        printf("%s got error = %d after wasm_loader_dynld_do_extern_reloc()\n", __func__, error);
    }

    // compile each and every module then instanciate
    error = wasm_loader_dynld_compile_and_run(l, dlctx, dlctx->dl_buf, dlctx->dl_bufsz);
    if (error != 0) {
        printf("%s got error = %d after wasm_loader_dynld_do_extern_reloc()\n", __func__, error);
    }

    // prepare to return to user-space but first create stack-point (TODO: this should be done in )
    uesp0 = howmany(stack_size_hint, WASM_PAGE_SIZE);
    error = wasm_exec_ioctl(546, (void *)&uesp0);
    if (error != 0) {
        printf("%s got error = %d when allocating user stack-memory)\n", __func__, error);
    }

    // TODO: destory dl_arena

    kmem_free(buf, bsize);

    // TODO: process the section-data here AND preform dylink operation if required by module!

    struct switchframe *sf;
    struct pcb *pcb;
    struct wasm32_execpkg_args *exec_arg;

    pcb = lwp_getpcb(curlwp);
    sf = (struct switchframe *)pcb->pcb_esp;

    if (sf->sf_ebx == (int)(NULL) || ((struct wasm32_execpkg_args *)sf->sf_ebx)->et_sign != WASM_EXEC_PKG_SIGN) {

        exec_arg = kmem_zalloc(sizeof(struct wasm32_execpkg_args), 0);
        exec_arg->et_sign = WASM_EXEC_PKG_SIGN;     
        exec_arg->et_type = RTLD_ET_EXEC_START;
        exec_arg->ps_addr = dlctx->libc_ps;
        exec_arg->__start = (int (*)(void(*)(void), struct ps_strings *))(dlctx->dl_start);
        
        sf->sf_esi = (int)wasm_exec_trampline;
        sf->sf_ebx = (int)exec_arg;
        sf->sf_eip = (int)lwp_trampoline;
        printf("%s setting switchframe at %p for lwp %p\n", __func__, sf, wasm_curlwp);
    }

    //wasm_exec_ioctl(547, NULL);

    return (0);
}

/**
 * Simply loads and executes a regular WebAssembly module.
 * At this stage its to late to change the Worker bindings, but it might be possible to provide wasi
 * bindings trough loadScripts() inside the worker.
 * TODO: enforce start to be a forbidden section, (tool to translate start into DT_START)
 */
int
wasm_loader_static_exec(struct lwp *l, struct exec_vmcmd *cmd)
{
    struct vnode *vp = cmd->ev_vp;
    struct vattr vap;
    struct wasm_processing_ctx *wa_mod;
    struct wasm_loader_args *args;
    size_t skiplen;
    char *buf;
    uint32_t ver;
    uint32_t off, bufsz, bsize, fsize;
    uint32_t exec_flags = 0;
    uint32_t exec_end_offset = 0;
    uint32_t stack_size_hint = 0;
    int32_t exec_bufid;
    bool is_dyn = false;
    int error;

    vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

    error = VOP_GETATTR(vp, &vap, l->l_cred);
    if (error != 0) {
        VOP_UNLOCK(vp);
        printf("%s error when VOP_GETATTR() called %d", __func__, error);
        return error;
    }

    fsize = vap.va_size;
    bsize = vap.va_blocksize;

    error = wasm_execbuf_alloc(fsize);
    if (error != 0) {
        VOP_UNLOCK(vp);
        printf("%s error when wasm_execbuf_alloc() called %d\n", __func__, error);
        return error;
    }

    printf("%s staring to load wasm executable of size = %d\n", __func__, fsize);

    wa_mod = kmem_zalloc(sizeof(struct wasm_processing_ctx), 0);
    if (wa_mod == NULL) {
        printf("%s could not alloc wa ctx... \n", __func__);
    }
    skiplen = 0;

    buf = kmem_alloc(bsize, 0);
    if (buf == NULL) {
        VOP_UNLOCK(vp);
        printf("%s ENOMEM when kmem_alloc() called\n", __func__);
        return ENOMEM;
    }

    off = 0;
    while (off < fsize) {
        bufsz = MIN(bsize, fsize - off);
        error = exec_read((struct lwp *)curlwp, vp, off, buf, bufsz, IO_NODELOCKED);
        if (error != 0) {
            printf("%s got error = %d from exec_read() \n", __func__, error);
        }

        // processing during loading
        if (skiplen < bufsz) {
            wasm_process_chunk(wa_mod, buf, bufsz, off, &skiplen);
        } else {
            skiplen -= bufsz;
        }

        wasm_execbuf_copy(buf, (void *)off, bufsz);

        // TODO: make assertion based upon module section content.
        off += bufsz;
    }

    VOP_UNLOCK(vp);

    kmem_free(buf, bsize);

    // TODO: process the section-data here AND preform dylink operation if required by module!

    kmem_free(wa_mod, sizeof(struct wasm_processing_ctx));

    wasm_exec_ioctl(547, NULL);

    return (0);
}


// finding dylibs

char sys_ld_paths[] = "/usr/lib:/lib:/GnuStep/System/Library/Libraries:/GnuStep/System/Library/Bundles";

int 
wasm_find_dylib(const char *name, const char *vers, char *buf, int32_t *bufsz, struct vnode **vpp) __WASM_EXPORT(wasm_find_dylib)
{
	// for each search-path find and interate trough directory.
	struct nameidata nd;
	struct pathbuf *pb;
	struct vnode *vp;
	struct vattr vap;
	struct uio uio;
	struct iovec iov;
	const char *next;
	char tmpbuf[128];
	size_t namelen;
	struct mm_page *pg;
	void *pgbuf;

	struct dirent *ent;
	char *inp;
	int len, reclen;
	off_t off;		/* true file offset */
	int buflen, eofflag;
	off_t *cookiebuf = NULL;
    off_t *cookie;
    int ncookies;
	struct lwp *l = (struct lwp *)curproc;
	const char *strp = sys_ld_paths;
    bool found = false;
	int error;
	size_t segl;

	pgbuf = NULL;
	namelen = strlen(name);

	while (true) {
		next = strchr(strp, ':');
		if (next == NULL) {
			segl = strlen(strp);
		} else {
			segl = next - strp;
		}
		strlcpy(tmpbuf, strp, segl + 1);
#if __WASM_DEBUG_KERN_DYLIB
		printf("%s search-path: '%s' (strlen = %zu) to find '%s' (strlen = %zu)\n", __func__, tmpbuf, segl, buf, namelen);
#endif

		pb = pathbuf_create(tmpbuf);
		NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF | TRYEMULROOT, pb);
		/* first get the vnode */
		if ((error = namei(&nd)) != 0) {
			printf("%s error-code = %d", __func__, error);
			pathbuf_destroy(pb);
		} else {

			printf("%s no error trigged", __func__);

			vp = nd.ni_vp;

			/* check access and type */
			if (vp->v_type != VDIR) {
				printf("%s search-path %s is not directory", __func__, tmpbuf);
			}
			if ((error = VOP_ACCESS(vp, VEXEC, l->l_cred)) != 0) {
				printf("%s error while checking for access", __func__);
			}

			/* XXX VOP_GETATTR is the only thing that needs LK_EXCLUSIVE here */
			if ((error = VOP_GETATTR(vp, &vap, l->l_cred)) != 0) {
				printf("%s error while reading attrs of vp", __func__);
			}

			off = 0;
			// reading directory in kernel-space, based on source-code in:
			// sys/compat/common/vfs_syscalls_30.c
			// TODO: needs to support reading all dirents of a directory, now its limited to reading only one chunk.

			if (pgbuf == NULL) {
				pgbuf = kmem_page_alloc(1, 0);
				if (pgbuf == NULL) {
					printf("%s ENOMEM", __func__);
					return ENOMEM;
				}

				// bypass file-mapping for exec read
				pg = paddr_to_page(pgbuf);
				pg->flags |= PG_BYPASS_FILE_MAP;

				buflen = PAGE_SIZE;
				iov.iov_base = pgbuf;
				iov.iov_len = buflen;
				uio.uio_iov = &iov;
				uio.uio_iovcnt = 1;
				uio.uio_rw = UIO_READ;
				uio.uio_resid = buflen;
				uio.uio_offset = off;
				UIO_SETUP_SYSSPACE(&uio);
			} else {
				iov.iov_base = pgbuf;
				iov.iov_len = buflen;
				uio.uio_resid = buflen;
				uio.uio_offset = off;
			}

			error = VOP_READDIR(vp, &uio, l->l_cred, &eofflag, &cookiebuf, &ncookies);
			if (error) {
				printf("%s error = %d from VOP_READDIR()", __func__, error);
				if (cookiebuf)
					kmem_free(cookiebuf, 0);
				kmem_page_free(pgbuf, 1);
				VOP_UNLOCK(vp);
				return error;
			}

			inp = pgbuf;
			if ((len = buflen - uio.uio_resid) == 0) {
				if (cookiebuf)
                    kmem_free(cookiebuf, 0);
                VOP_UNLOCK(vp);
            } else {

			    for (cookie = cookiebuf; len > 0; len -= reclen) {
                    ent = (struct dirent *)inp;
                    reclen = ent->d_reclen;
                    inp += reclen;
                    if (reclen & _DIRENT_ALIGN(ent)) {
                        panic("%s: bad reclen %d", __func__, reclen);
                    }
                    if (cookie) {
                        off = *cookie++; /* each entry points to the next */
                    } else {
                        off += reclen;
                    }

                    if ((ent->d_namlen == 1 && ent->d_name[0] == '.') || (ent->d_namlen == 2 && ent->d_name[0] == '.' && ent->d_name[1] == '.')) {
                        continue;
                    }

#if __WASM_DEBUG_KERN_DYLIB
				    printf("%s ent->d_type = %d ent->d_name = %s\n", __func__, ent->d_type, ent->d_name);
#endif

				    if (ent->d_type == VREG || ent->d_type == DT_LNK) {

                        if (ent->d_namlen < namelen) {
                            printf("ent->d_namlen < namelen");
                            continue;
                        }

                        if (strncmp(ent->d_name, name, namelen) == 0) {

                            int newlen = (segl + ent->d_namlen + 1);
                            strlcpy(buf, tmpbuf, segl + 1);
                            buf[segl] = '/';
                            strlcpy(buf + segl + 1, ent->d_name, ent->d_namlen + 1);
                            *bufsz = newlen;
                            printf("%s found path '%s'\n", __func__, buf);
                            if (ent->d_type == DT_LNK) {
                                VOP_UNLOCK(vp);
                                pb = pathbuf_create(buf);
                                NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF | TRYEMULROOT, pb);
                                error = namei(&nd);
                                if (error != 0) {
                                    printf("%s got error = %d from namei() for symlink..\n", __func__, error);
                                } else {
                                    vp = nd.ni_vp;
                                    vnode_to_path(buf, MAXPATHLEN, vp, l, l->l_proc);
                                    printf("%s followed symlink to path '%s'\n", __func__, buf);
                                    *bufsz = strlen(buf);
                                }
                            }
                            found = true;
                            //goto out;
                            break;
                        }

				    } else if (ent->d_type == DT_DIR) {

					    // if ends with ".framework"
					    const char *extstr = strrchr(ent->d_name, '.');

#if __WASM_DEBUG_KERN_DYLIB
					    printf("%s ext-str = '%s'\n matches (#1) = %d matches (#1) = %d", __func__, extstr, strcmp(extstr, ".framework"), strncmp(ent->d_name, namebuf, namelen));
#endif

                        if (extstr != NULL && strcmp(extstr, ".framework") == 0 && strncmp(ent->d_name, name, namelen) == 0) {
                            int newlen = (segl + ent->d_namlen + namelen + 2);
                            char *strp = buf;
                            strlcpy(strp, tmpbuf, segl + 1);
                            strp += segl;
                            *strp = '/';
                            strp++;
                            strlcpy(strp, ent->d_name, ent->d_namlen + 1);
                            strp += ent->d_namlen;
                            *strp = '/';
                            strp++;
                            strlcpy(strp, name, namelen + 1);
                            *bufsz = (strp - buf) + namelen;
                            printf("%s found path '%s'\n", __func__, buf);
                            found = true;
                            //goto out;
                            break;
                        }

                    } else {
                        continue;
                    }
			    }

                if (cookiebuf) {
		            kmem_free(cookiebuf, 0);
                    cookiebuf = NULL;
                }

                VOP_UNLOCK(vp);

                if (found) {
                    break;
                }
            }

		}
		
		if (next != NULL) {
			strp = next + 1;
		} else {
			break;
		}
	}

    kmem_page_free(pgbuf, 1);

    vput(vp);

    if (found) {
        return (0);
    }

    printf("%s did not find '%s'\n", __func__, name);

	return ENOENT;

eof:
	if (cookiebuf)
		kmem_free(cookiebuf, 0);
	kmem_page_free(pgbuf, 1);
	VOP_UNLOCK(vp);

	return 0;

out:
	if (cookiebuf)
		kmem_free(cookiebuf, 0);
	kmem_page_free(pgbuf, 1);
	VOP_UNLOCK(vp);

	return 0;
}

//
// Implementation that expect the whole binary being available wihout the use if read-back operations.
//

/**
 * In-memory variant of 
 * @param 
 */
int
rtld_find_module_memory(struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module, int execfd, char *buf, uint32_t bufsz, struct wasm_loader_meminfo *meminfo)
{   
    const char *module_name;
    const char *name;
    struct wa_section_info *sec;
    uint32_t module_namesz, namesz, count, lebsz;
    uint8_t kind;
    uint8_t *ptr, *ptr_start, *end, *file_start, *sym_start;
    const char *errstr = NULL;

    sec = wasm_find_section(module, WASM_SECTION_IMPORT, NULL);
    if (sec == NULL) {
        printf("%s could not find import section\n", __func__);
    }

    file_start = module->wa_filebuf;
    ptr_start = file_start + sec->wa_offset;
    ptr = ptr_start;
    end = ptr_start + sec->wa_size;
    errstr = NULL;

    /*
    if (*(ptr) != WASM_SECTION_IMPORT) {
        printf("%s buffer start is not import section %02x found\n", __func__, *(ptr));
        return ENOEXEC;
    }
    ptr++;
    */

    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    for (int i = 0; i < count; i++) {
        sym_start = ptr;
        lebsz = 0;
        module_namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        module_name = (const char *)ptr;
        ptr += module_namesz;

        lebsz = 0;
        namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        name = (const char *)ptr;
        ptr += namesz;

        kind = *(ptr);
        ptr++;

        if (kind == WASM_IMPORT_KIND_FUNC) {
            uint32_t typeidx = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
        } else if (kind == WASM_IMPORT_KIND_TABLE) {
            uint32_t min;
            uint32_t max;
            uint8_t reftype;
            uint8_t limit;
            reftype = *(ptr);
            ptr++;
            limit = *(ptr);
            ptr++;
            if (limit == 0x01) {
                min = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;

                max = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
            } else if (limit == 0x00) {
                min = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
            }

        } else if (kind == WASM_IMPORT_KIND_MEMORY) {
            uint32_t min;
            uint32_t max;
            uint32_t min_file_offset;
            uint32_t min_lebsz;
            uint32_t max_lebsz;
            bool shared;
            uint8_t limit;
            limit = *(ptr);
            ptr++;
            min_lebsz = 0;
            max_lebsz = 0;
            min_file_offset = ptr - file_start;
            if (limit == 0x01) {
                min = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                min_lebsz = lebsz;

                max = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                max_lebsz = lebsz;
                shared = false;
            } else if (limit == 0x00) {
                min = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                min_lebsz = lebsz;
                shared = false;
            } else if (limit == 0x02) {
                min = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                min_lebsz = lebsz;
                shared = true;
            } else if (limit == 0x03) {
                min = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                min_lebsz = lebsz;

                max = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                max_lebsz = lebsz;
                shared = true;
            }

            if (meminfo) {
                meminfo->limit = limit;
                meminfo->min = min;
                meminfo->max = max;
                meminfo->min_file_offset = min_file_offset;
                meminfo->min_lebsz = min_lebsz;
                meminfo->max_lebsz = max_lebsz;
                meminfo->shared = shared;
            }

            return (0);
        } else if (kind == WASM_IMPORT_KIND_GLOBAL) {
            uint8_t type;
            uint8_t mutable;

            type = *(ptr);
            ptr++;
            mutable = *(ptr);
            ptr++;
        } else if (kind == WASM_IMPORT_KIND_TAG) {
            uint8_t attr;
            uint32_t typeidx;

            attr = *(ptr);
            ptr++;
            typeidx = decodeULEB128(ptr, &lebsz, end, &errstr);

        }
    }

    return 0;
}

/**
 * In-memory variant of 
 *
 */
int
rtld_do_internal_reloc_on_module(struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module)
{
    struct wasm_loader_module_dylink_state *dl_state;
    struct wasm_loader_dylink_section *section;
    struct wa_data_segment_info *data_segments;
    struct wa_elem_segment_info *elem_segments;
    struct wa_section_info *code_section;
    int error;
    uint32_t elem_count, data_count;
    uint32_t count, lebsz;
    uint8_t *ptr, *end, *ptr_start, *file_start, *rloc, *sec_start;
    const char *errstr;
    union i32_value i32;

    // find section
    code_section = NULL;
    dl_state = module->wa_dylink_data;
    section = wasm_dylink0_find_subsection(dl_state, NBDL_SUBSEC_RLOCINT);
    if (module->wa_filebuf == NULL || section == NULL) {
        return EINVAL;
    }

    file_start = (uint8_t *)module->wa_filebuf;
    ptr = file_start + section->src_offset;
    end = ptr + section->size;
    ptr_start = ptr;
    errstr = NULL;
    
    // TODO: this loading must adopt to reading back chunks..

    elem_segments = module->wa_elem_segments;
    elem_count = module->wa_elem_count;

    data_segments = module->wa_data_segments;
    data_count = module->wa_data_count;

    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    for(int i = 0; i < count; i++) {
        uint32_t chunksz, dst_idx, src_type, src_idx, rloc_count, dst_off, dst_base, dst_max, dst_addr, src_off, src_base;
        uint8_t rloctype;

        rloctype = *(ptr);
        ptr++;
        chunksz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        sec_start = ptr;
        // only R_WASM_MEMORY_ADDR_I32 has dst_idx
        if (rloctype == R_WASM_MEMORY_ADDR_I32 || rloctype == R_WASM_TABLE_INDEX_I32) {
            dst_idx = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            if (dst_idx >= data_count) {
                //printf("%s dst_idx %d to large\n", __func__, dst_idx);
            }
            dst_base = data_segments[dst_idx].wa_src_offset;
            dst_max = (dst_base + data_segments[dst_idx].wa_size) - 4;
            //printf("%s dst_base = %d of rloctype = %d\n", __func__, dst_base, rloctype);

            src_type = *(ptr);
            ptr++;
            src_idx = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            if (src_type == 1) { // data-segment
                if (src_idx >= data_count) {
                    printf("%s src_idx %d to large for data count %d\n", __func__, src_idx, data_count);
                    error = EINVAL;
                    goto errout;
                }
                src_base = data_segments[src_idx].wa_dst_offset;
            } else if (src_type == 2) { // elem-segment
                if (src_idx >= elem_count) {
                    printf("%s src_idx %d to large for elem count %d\n", __func__, src_idx, elem_count);
                    error = EINVAL;
                    goto errout;
                }
                src_base = elem_segments[src_idx].wa_dst_offset;
            } else {
                printf("%s INVALID_SRC_TYPE = %d\n", __func__, src_type);
                error = EINVAL;
                goto errout;
            }

            rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            //printf("%s src_base = %d of src_type = %d rloc_count = %d\n", __func__, src_base, src_type, rloc_count);

            for (int x = 0; x < rloc_count; x++) {
                dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                dst_addr = dst_base + dst_off;                
                if (dst_addr < dst_base || dst_addr > dst_max) {
                    printf("%s ERROR i32_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d) segment = %s\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base, data_segments[dst_idx].wa_name);
                }

                i32.value = src_base + src_off;
                rloc = file_start + dst_addr;
                rloc[0] = i32.bytes[0];
                rloc[1] = i32.bytes[1];
                rloc[2] = i32.bytes[2];
                rloc[3] = i32.bytes[3];
            }

        } else {
            src_type = *(ptr);
            ptr++;
            src_idx = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            if (code_section == NULL) {
                code_section = wasm_find_section(module, WASM_SECTION_CODE, NULL);
                if (code_section == NULL) {
                    printf("%s ERROR could not find code-section for code-reloc\n", __func__);
                    error = ENOENT;
                    goto errout;
                }
            }

            dst_base = code_section->wa_offset;
            dst_max = (dst_base + code_section->wa_size) - 5;

            if (src_type == 1) { // data-segment
                if (src_idx >= data_count) {
                    printf("%s ERROR src_idx %d to large for data count %d\n", __func__, src_idx, data_count);
                    error = EINVAL;
                    goto errout;
                }
                src_base = data_segments[src_idx].wa_dst_offset;
                //printf("%s src_base = %d of type = %d\n", __func__, src_base, src_type);
            } else if (src_type == 2) { // elem-segment
                if (src_idx >= elem_count) {
                    printf("%s ERROR src_idx %d to large for elem count %d\n", __func__, src_idx, elem_count);
                    error = EINVAL;
                    goto errout;
                }
                src_base = elem_segments[src_idx].wa_dst_offset;
                //printf("%s src_base = %d of type = %d\n", __func__, src_base, src_type);
            } else {
                //printf("%s INVALID_SRC_TYPE = %d\n", __func__, src_type);
                error = EINVAL;
                goto errout;
            }

            rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            if (rloctype == R_WASM_MEMORY_ADDR_LEB) {

                for (int x = 0; x < rloc_count; x++) {
                    dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    dst_addr = dst_base + dst_off;
                    if (dst_addr <= dst_base || dst_addr > dst_max) {
                        printf("%s ERROR leb_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d)\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base);
                    }
                    rloc = file_start + dst_addr;
                    encodeULEB128(src_base + src_off, rloc, 5);
                }

            } else if (rloctype == R_WASM_MEMORY_ADDR_SLEB || rloctype == R_WASM_TABLE_INDEX_SLEB) {

                for (int x = 0; x < rloc_count; x++) {
                    dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    dst_addr = dst_base + dst_off;
                    if (dst_addr < dst_base || dst_addr > dst_max) {
                        printf("%s ERROR leb_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d)\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base);
                    }
                    rloc = file_start + dst_addr;
                    encodeSLEB128(src_base + src_off, rloc, 5);
                }
            }
        }

        ptr = sec_start + chunksz;
    }

    dbg_loading("%s did all relocs\n", __func__);

    return (0);

errout: 

    printf("%s ERROR %d failed to complete relocs\n", __func__, error);

    return error;

}

/**
 * In-memory variant of 
 *
 */
int
rtld_reloc_place_segments(struct lwp *l, struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module)
{
    struct mm_arena *dl_arena;
    struct wa_data_segment_info *data_seg;
    struct wa_elem_segment_info *elem_seg;
    struct wash_exechdr_rt *hdr;
    uint32_t wapgs;
    uint32_t segcount;
    int error;
    union {
        struct wasm_loader_cmd_cp_kmem_to_umem cp_kmem;
        struct wasm_loader_cmd_mk_umem mkmem;
        struct wasm_loader_cmd_umem_grow mgrow;
        struct wasm_loader_cmd_umem_grow tblgrow;
        struct wasm_loader_cmd_mk_table mktbl;
    } exec_cmd;

    uint32_t mem_start = dlctx->dl_membase; // TODO: use option but have default at 4096
    uint32_t mem_off = mem_start;
    uint32_t mem_pad = 0;
    uint32_t tbl_start = dlctx->dl_tblbase;
    uint32_t tbl_off = tbl_start;
    uint32_t tbl_pad = 0;
    uint32_t module_rt_size, module_rt_off;
    struct wasm_module_rt *rtld_module_src;

    dl_arena = dlctx->dl_arena;
    // compute initial data-segments
    segcount = module->wa_data_count;
    data_seg = module->wa_data_segments;
    printf("%s module-name = %s address = %p\n", __func__, module->wa_module_name, module);
    for (int x = 0; x < segcount; x++) {
        
        mem_off = alignUp(mem_off, data_seg->wa_align, &mem_pad);
        data_seg->wa_dst_offset = mem_off;
        mem_off += data_seg->wa_size;
        printf("%s placing %s %s at %d\n", __func__, module->wa_module_name, data_seg->wa_name, data_seg->wa_dst_offset);
        // finding .dynsym section for later use.
        if (module->wa_dylink_data != NULL && module->wa_dylink_data->dl_dlsym == NULL && data_seg->wa_name != NULL && strncmp(data_seg->wa_name, ".dynsym", 7) == 0) {
            module->wa_dylink_data->dl_dlsym = data_seg;
        }

        data_seg++;
    }

    // computing needed memory for all struct wasm_module_rt
    // used in dynamic-library info struct for dlsym/dlinfo/dlopen etc.
    module_rt_size = sizeof(struct wasm_module_rt);
    mem_off = alignUp(mem_off, 8, &mem_pad);
    module_rt_off = mem_off;
    mem_off += module_rt_size;

    // compute initial element-segments
    // all objc_indirect_functions are appended after each other
    segcount = module->wa_elem_count;
    elem_seg = module->wa_elem_segments;
    for (int x = 0; x < segcount; x++) {
        if (elem_seg->wa_name != NULL && (strncmp(elem_seg->wa_name, "objc_indirect_functions", 23) == 0 || strncmp(elem_seg->wa_name, ".objc_indirect_funcs", 20) == 0)) {
            // do nothing
        } else {
            tbl_off = alignUp(tbl_off, elem_seg->wa_align, &tbl_pad);
            elem_seg->wa_dst_offset = tbl_off;
            tbl_off += elem_seg->wa_size;
            dbg_loading("%s placing %s %s at %d\n", __func__, module->wa_module_name, elem_seg->wa_name, elem_seg->wa_dst_offset);
        }
        elem_seg++;
    }

    // placing all objc_indirect_functions & .objc_indirect_funcs element segments
    for (int x = 0; x < segcount; x++) {
        if (elem_seg->wa_name != NULL && (strncmp(elem_seg->wa_name, "objc_indirect_functions", 23) == 0 || strncmp(elem_seg->wa_name, ".objc_indirect_funcs", 20) == 0)) {
            tbl_off = alignUp(tbl_off, elem_seg->wa_align, &tbl_pad);
            elem_seg->wa_dst_offset = tbl_off;
            tbl_off += elem_seg->wa_size;
            dbg_loading("%s placing %s %s at %d\n", __func__, module->wa_module_name, elem_seg->wa_name, elem_seg->wa_dst_offset);
        }
        elem_seg++;
    }

    if (dlctx->dl_mem_created == false) {
        // create env.__linear_memory
        exec_cmd.mkmem.min = 10;
        exec_cmd.mkmem.max = 4096;      // FIXME: take from wa_mod->meminfo
        exec_cmd.mkmem.shared = true;
        exec_cmd.mkmem.bits = 32;
        error = wasm_exec_ioctl(EXEC_IOCTL_MAKE_UMEM, &exec_cmd);
        if (error != 0) {
            printf("%s got error = %d after growing memory with delta = %d (WASM_PAGE_SIZE)\n", __func__, error, wapgs);
        }
        dlctx->dl_mem_created = true;
        // FIXME: should store metadata about memory
#if 0
        dlctx->dl_memory = mm_arena_zalloc_simple(dl_arena, sizeof(struct wasm_loader_meminfo), NULL);
        if (dlctx->dl_memory == NULL) {
            printf("%s got error = %d creating meminfo\n", __func__, error);
        }
#endif
    }

    // grow env.__linear_memory
    wapgs = howmany(mem_off, WASM_PAGE_SIZE);
    exec_cmd.mgrow.grow_size = wapgs;
    exec_cmd.mgrow.grow_ret = 0;
    error = wasm_exec_ioctl(EXEC_IOCTL_UMEM_GROW, &exec_cmd);
    if (error != 0) {
        printf("%s got error = %d after growing memory with delta = %d (WASM_PAGE_SIZE)\n", __func__, error, wapgs);
    }

    if (dlctx->dl_tbl_created == false) {
        // create env.__indirect_table
        exec_cmd.mktbl.min = tbl_start;
        exec_cmd.mktbl.max = -1;
        exec_cmd.mktbl.reftype = 0x70;
        error = wasm_exec_ioctl(EXEC_IOCTL_UTBL_MAKE, &exec_cmd);
        if (error != 0) {
            printf("%s got error = %d after creating table with desc {min: %d max: %d reftype: %d}\n", __func__, error, exec_cmd.mktbl.min, exec_cmd.mktbl.max, exec_cmd.mktbl.reftype);
        }
        dlctx->dl_tbl_created = true;
    }

    // grow env.__indirect_table
    exec_cmd.tblgrow.grow_size = tbl_off - tbl_start;
    exec_cmd.tblgrow.grow_ret = 0;
    error = wasm_exec_ioctl(EXEC_IOCTL_UTBL_GROW, &exec_cmd);
    if (error != 0) {
        printf("%s got error = %d after growing table with delta = %d", __func__, error, exec_cmd.tblgrow.grow_size);
    }

    // do internal code/data relocs
    rtld_do_internal_reloc_on_module(dlctx, module);

    dlctx->dl_membase = mem_off;
    dlctx->dl_tblbase = tbl_off;

    // do not copy segments yet

    // after relocation we need to update offset for __start if provided
    hdr = module->wa_exechdr;

    if (hdr != NULL && hdr->exec_start_elemidx != -1 && hdr->exec_start_funcidx != -1) {
        int32_t exec_start_elemidx = hdr->exec_start_elemidx;
        int32_t exec_start_funcidx = hdr->exec_start_funcidx;

        segcount = module->wa_elem_count;
        elem_seg = module->wa_elem_segments;
        if (exec_start_elemidx < 0 || exec_start_elemidx >= segcount) {
            exec_start_elemidx = -1;
        }

        if (exec_start_elemidx >= 0) {
            elem_seg += exec_start_elemidx;

            if (exec_start_funcidx < 0 || exec_start_funcidx >= elem_seg->wa_size) {
                exec_start_funcidx = -1;
            } else {
                exec_start_funcidx = elem_seg->wa_dst_offset + exec_start_funcidx;
            }
        }

        hdr->exec_start_elemidx = exec_start_elemidx;
        hdr->exec_start_funcidx = exec_start_funcidx;
    }
    


    // creating and copying all module_rt data
    rtld_module_src = (struct wasm_module_rt *)dlctx->dl_buf;
    wasm_memory_fill(rtld_module_src, 0, sizeof(struct wasm_module_rt));
    hdr = module->wa_exechdr;
    if (hdr != NULL) {
        rtld_module_src->__start = (void (*)(void (*)(void), struct ps_strings *))(hdr->exec_start_funcidx != -1 ? hdr->exec_start_funcidx : 0);
    }

    if (module->wa_dylink_data && module->wa_dylink_data->dl_dlsym) {
        data_seg = module->wa_dylink_data->dl_dlsym;
        rtld_module_src->dlsym_start = (void *)data_seg->wa_dst_offset;
        rtld_module_src->dlsym_end = (void *)(data_seg->wa_dst_offset + data_seg->wa_size);
    }

    data_seg = wasm_rtld_find_data_segment(module, ".init_array");
    if (data_seg) {
        rtld_module_src->init_array = (void *)data_seg->wa_dst_offset;
        rtld_module_src->init_array_count = (data_seg->wa_size / sizeof(void *));
    }

    data_seg = wasm_rtld_find_data_segment(module, ".fnit_array");
    if (data_seg) {
        rtld_module_src->fnit_array = (void *)data_seg->wa_dst_offset;
        rtld_module_src->fnit_array_count = (data_seg->wa_size / sizeof(void *));
    }

    exec_cmd.cp_kmem.buffer = module->wa_module_desc_id;
    exec_cmd.cp_kmem.dst_offset = module_rt_off;
    exec_cmd.cp_kmem.src = rtld_module_src;
    exec_cmd.cp_kmem.size = sizeof(struct wasm_module_rt);
    error = wasm_exec_ioctl(EXEC_IOCTL_CP_KMEM_TO_UMEM, &exec_cmd);
    if (error != 0) {
        printf("%s got error = %d from cpy kmem to umem\n", __func__, error);
    }

    module->wa_dylink_data->rtld_modulep = (struct wasm_module_rt *)module_rt_off;
    module_rt_off += sizeof(struct wasm_module_rt);

    return (0);
}

/**
 * In-memory variant of 
 */
int
rtld_do_extern_reloc_on_module(struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module)
{
    // extern reloc is similar to internal, only that these are weakly linked trough a name which are checked against other modules.
    struct wasm_loader_module_dylink_state *dl_state;
    struct wasm_loader_dylink_section *section;
    struct wa_data_segment_info *data_segments;
    struct wa_section_info *code_section;
    char *name;
    uint32_t namesz;
    int error;
    uint32_t data_count;
    uint32_t count, lebsz;
    uint32_t module_count;
    uint32_t execfd;
    uint32_t rloc_count;
    struct wa_module_info **modules;
    struct wa_module_info *submod;
    uint8_t *ptr, *end, *ptr_start, *file_start, *chunk_start;
    const char *errstr;
    union i32_value i32;
    union {
        struct wasm_loader_dynld_dlsym dlsym;
    } exec_cmd;

    // find section
    code_section = NULL;
    execfd = module->wa_module_desc_id;
    dl_state = module->wa_dylink_data;
    section = wasm_dylink0_find_subsection(dl_state, NBDL_SUBSEC_RLOCEXT);
    if (section == NULL) {
        return EINVAL;
    }

    dbg_loading("%s external reloc on module = %s addr = %p\n", __func__, module->wa_module_name, module);

    file_start = (uint8_t *)module->wa_filebuf;
    ptr_start = file_start + section->src_offset;
    ptr = ptr_start;
    end = ptr_start + section->size;
    errstr = NULL;

    data_segments = module->wa_data_segments;
    data_count = module->wa_data_count;

    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    dbg_loading("%s count %d module = %s\n", __func__, count, module->wa_module_name);


    // 
    module_count = dlctx->dl_module_count;
    modules = dlctx->dl_modules;

    code_section = wasm_find_section(module, WASM_SECTION_CODE, NULL);
    if (code_section == NULL) {
        printf("%s ERROR could not find code-section for code-reloc\n", __func__);
        error = ENOENT;
        goto errout;
    }

    for(int i = 0; i < count; i++) {
        uint32_t chunksz, dst_idx, src_type, src_idx, rloc_count, dst_off, src_off, src_base;
        uint8_t symtype;
        uint8_t *dst_base, *dst_max, *dst_addr, *dst;
        bool found;

        symtype = *(ptr);
        ptr++;
        chunksz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        chunk_start = ptr;
        namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        name = (char *)ptr;
        ptr += namesz;

        if (symtype == 0) {
            symtype = 1;
        } else if (symtype == 1) {
            symtype = 2;
        } else {
            printf("non-supported symtype %d in module %s\n", symtype, module->wa_module_name);
            symtype = 255;
        }

        found = false;

        for (int x = 0; x < module_count; x++) {

            submod = modules[x];
            if (submod == module) {
                continue;
            }

            if (submod->wa_dylink_data == NULL || submod->wa_dylink_data->dl_dlsym == NULL) {
                printf("%s module = %s (%p) missing .dynsym data..\n", __func__, submod->wa_module_name, submod);
                continue;
            }

            struct wa_data_segment_info *dlsym = submod->wa_dylink_data->dl_dlsym;

            if (dlsym == NULL)
                continue;
            
            exec_cmd.dlsym.dynsym_start = (void *)dlsym->wa_dst_offset;
            exec_cmd.dlsym.dynsym_end = (void *)(dlsym->wa_dst_offset + dlsym->wa_size);
            exec_cmd.dlsym.symbol_name = name;
            exec_cmd.dlsym.symbol_size = namesz;
            exec_cmd.dlsym.symbol_type = symtype;
            exec_cmd.dlsym.symbol_addr = 0;
            error = wasm_exec_ioctl(EXEC_IOCTL_DYNLD_DLSYM_EARLY, &exec_cmd);
            if (error != 0) {
                continue;
            }

            found = true;
            src_base = exec_cmd.dlsym.symbol_addr;
            dbg_loading("%s found symbol '%s' on module '%s' at = %p\n", __func__, name, submod->wa_module_name, (void *)src_base);
        }

        if (!found) {
            printf("%s symbol '%s' not found (namesz = %d chunksz = %d)\n", __func__, name, namesz, chunksz);
            ptr = chunk_start + chunksz;
            continue;
        }

        // uleb relocs
        rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (rloc_count > 0) {

            dst_base = file_start + code_section->wa_offset;
            dst_max = dst_base + (code_section->wa_size - 5);

            for (int x = 0; x < rloc_count; x++) {
                dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                dst_addr = dst_base + dst_off;
                if (dst_addr <= dst_base || dst_addr > dst_max) {
                    printf("%s ERROR leb_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d)\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base);
                }
                encodeULEB128(src_base + src_off, dst_addr, 5);
            }
        }

        // sleb relocs
        rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (rloc_count > 0) {

            dst_base = file_start + code_section->wa_offset;
            dst_max = dst_base + (code_section->wa_size - 5);

            for (int x = 0; x < rloc_count; x++) {
                dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                dst_addr = dst_base + dst_off;
                if (dst_addr <= dst_base || dst_addr > dst_max) {
                    printf("%s ERROR leb_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d)\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base);
                }
                encodeSLEB128(src_base + src_off, dst_addr, 5);
            }
        }

        // data relocs
        rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (rloc_count > 0) {
            
            for (int x = 0; x < rloc_count; x++) {
                dst_idx = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;

                if (dst_idx >= data_count) {
                    printf("%s dst_idx %d to large (in %d of %d, ptr = %p ptr_start %p)\n", __func__, dst_idx, x, rloc_count, ptr, ptr_start);
                }
                dst_base = file_start + data_segments[dst_idx].wa_src_offset;
                dst_max = dst_base + (data_segments[dst_idx].wa_size - 4);
                dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                dst_addr = dst_base + dst_off;
                if (dst_addr < dst_base || dst_addr > dst_max) {
                    printf("%s ERROR i32_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d) segment = %s\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base, data_segments[dst_idx].wa_name);
                }
                i32.value = src_base + src_off;
                dst = dst_addr;
                dst[0] = i32.bytes[0];
                dst[1] = i32.bytes[1];
                dst[2] = i32.bytes[2];
                dst[3] = i32.bytes[3];
            }
        }

        ptr = chunk_start + chunksz;
    }

    printf("%s did all external relocs\n", __func__);

    return (0);

errout: 

    printf("%s failed to complete relocs\n", __func__);

    return error;
}

/**
 * In-memory variant of wasm_loader_dynld_do_extern_reloc
 *
 */
int
rtld_do_extern_reloc(struct lwp *l, struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module)
{
    struct wa_data_segment_info *data_seg;
    uint32_t count, segcount;
    int error;
    union {
        struct wasm_loader_cmd_cp_kmem_to_umem cp_kmem;
    } exec_cmd;

    // resolve dlsym based symbol-references & external dependant code/data relocs
    error = rtld_do_extern_reloc_on_module(dlctx, module);
    if (error != 0)
        printf("%s got error = %d from wasm_loader_extern_reloc_on_module() %s \n", __func__, error, module->wa_module_name);

    // copy memory segments into user-space memory
    segcount = module->wa_data_count;
    data_seg = module->wa_data_segments;
    printf("%s now copying %d data-segments from kmem to umem\n", __func__, segcount);
    for (int x = 0; x < segcount; x++) {
        if (data_seg->wa_dst_offset != 0) {
            
            printf("%s copying data at %d name = %s from %p (kmem) to %p (umem) of size = %d\n", __func__, x, data_seg->wa_name, (void *)(module->wa_filebuf + data_seg->wa_src_offset), (void *)(data_seg->wa_dst_offset), data_seg->wa_size);

            exec_cmd.cp_kmem.buffer = -1;
            exec_cmd.cp_kmem.dst_offset = data_seg->wa_dst_offset;
            exec_cmd.cp_kmem.size = data_seg->wa_size;
            exec_cmd.cp_kmem.src = (void *)(module->wa_filebuf + data_seg->wa_src_offset);
            error = wasm_exec_ioctl(EXEC_IOCTL_CP_KMEM_TO_UMEM, &exec_cmd);
            if (error != 0) {
                printf("%s got error = %d from EXEC_IOCTL_CP_KMEM_TO_UMEM\n", __func__, error);
            }

        } else {
            printf("%s ERROR %s %s missing wa_dst_offset\n", __func__, module->wa_module_name, data_seg->wa_name);
        }
        data_seg++;
    }

    return (0);
}

#if 0
int
rtld_read_data_segments_info(struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module, struct wa_section_info *section)
{
    struct mm_arena *dl_arena;
    struct wa_data_segment_info *wa_data;
    uint8_t kind;
    uint32_t count, lebsz;
    uint8_t *ptr, *end, *ptr_start, *file_start;
    const char *errstr;
    
    if (section->wa_type != WASM_SECTION_DATA) {
        return EINVAL;
    }

    dl_arena = dlctx->dl_arena;
    errstr = NULL;

    file_start = (uint8_t *)module->wa_filebuf;
    ptr_start = file_start + section->wa_offset;
    end = ptr_start + section->wa_size;
    ptr = ptr_start;
    
    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    wa_data = mm_arena_zalloc_simple(dl_arena, sizeof(struct wa_data_segment_info) * count, NULL);
    if (wa_data == NULL) {
        return ENOMEM;
    }
    module->wa_data_count = count;
    module->wa_data_segments = wa_data;

    for (int i = 0; i < count; i++) {
        uint32_t size;
        uint32_t kind = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (kind != 0x01) {
            printf("%s found non passive data-segment near %lu\n", __func__, ptr - file_start);
            break;
        }
        size = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        wa_data->wa_type = kind;
        wa_data->wa_size = size;
        wa_data->wa_src_offset = (uint32_t)(ptr - file_start);

        printf("%s data-segment type = %d size = %d src_offset = %d\n", __func__, wa_data->wa_type, wa_data->wa_size, wa_data->wa_src_offset);
        wa_data++;
        ptr += size;
    }

    // uleb kind (should be u8 I think?)
    // uleb size
    // bytes[size]
    // 
    // only kind == 0x01 is easy to support (we could also put data-segment into a custom section?)

    return (0);
}
#endif

int
rtld_read_dylink0_subsection_info(struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module, struct wa_section_info *section)
{
    struct mm_arena *dl_arena;
    struct wasm_loader_module_dylink_state *dl_state;
    struct wasm_loader_dylink_section *subsec;
    uint32_t namesz;
    uint8_t kind;
    uint32_t count, lebsz;
    uint8_t *ptr, *ptr_start, *end, *file_start;
    const char *errstr;
    
    if (section->wa_type != WASM_SECTION_CUSTOM) {
        return EINVAL;
    }

    dl_arena = dlctx->dl_arena;
    errstr = NULL;

    file_start = (uint8_t *)module->wa_filebuf;
    ptr_start = file_start + section->wa_offset;
    end = ptr_start + section->wa_size;
    ptr = ptr_start;

    namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;
    if (namesz != 13 || strncmp((const char *)ptr, "rtld.dylink.0", namesz) != 0) {
        printf("%s not a rtld.dylink.0 section..\n", __func__);
        return EINVAL;
    }
    ptr += namesz;

    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    if (count > 8) {
        printf("%s current ABI specifies 8 segment types and all unique, why is there %d\n",__func__, count);
        return EINVAL;
    }
    
    dl_state = mm_arena_zalloc_simple(dl_arena, sizeof(struct wasm_loader_module_dylink_state), NULL);
    if (!dl_state) {
        printf("%s failed to alloc dl_state\n", __func__);
        return ENOMEM;
    }

    dl_state->subsection_count = count;
    subsec = dl_state->wa_subsections;

    for (int i = 0; i < count; i++) {
        uint32_t size;
        uint32_t kind = *(ptr);
        ptr++;
        size = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        subsec->kind = kind;
        subsec->size = size;
        subsec->src_offset = ptr - file_start;

        printf("%s dylink-subsection kind = %d size = %d src_offset = %d\n", __func__, subsec->kind, subsec->size, subsec->src_offset);

        subsec++;
        ptr += size;
    }

    module->wa_dylink_data = dl_state;

    return (0);
}

/**
 * Retrives the following parameters from the `netbsd.dylink0` section:
 * - self module name + version
 * - needed module names + version
 * - element segments info.
 * - data segments info.
 */
int
rtld_dylink0_decode_modules(struct wasm_loader_dl_ctx *dlctx, struct wa_module_info *module, struct wasm_loader_module_dylink_state *dl_state)
{
    struct mm_arena *dl_arena;
    struct wa_section_info *data_sec;
    struct wasm_loader_dylink_section *section;
    struct wa_elem_segment_info *wa_elem;
    struct wa_module_needed *wa_needed;
    struct wa_data_segment_info *arr;
    struct wa_data_segment_info *seg;
    char *vers;
    char *name;
    uint32_t verssz;
    uint32_t namesz;
    uint32_t count, lebsz;
    uint32_t min_data_off, max_data_off, data_off_start;
    uint8_t *ptr, *end, *ptr_start, *file_start;
    const char *errstr;

    // find section
    section = wasm_dylink0_find_subsection(dl_state, NBDL_SUBSEC_MODULES);
    if (section == NULL) {
        return EINVAL;
    }

    dl_arena = dlctx->dl_arena;

    file_start = (uint8_t *)module->wa_filebuf;
    ptr_start = file_start + section->src_offset;
    end = ptr_start + section->size;
    ptr = ptr_start;
    errstr = NULL;
    
    namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;
    if (namesz != 0) {
        name = mm_arena_alloc_simple(dl_arena, namesz + 1, NULL);
        strlcpy(name, (const char *)ptr, namesz + 1);
        ptr += namesz;
    }
    module->wa_module_name = name;

    vers = NULL;
    verssz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;
    if (verssz != 0) {
        vers = mm_arena_alloc_simple(dl_arena, verssz + 1, NULL);
        strlcpy(vers, (const char *)ptr, verssz + 1);
        ptr += verssz;
    }
    module->wa_module_vers = vers;

    printf("%s module-name = '%s' module-vers = '%s'\n", __func__, module->wa_module_name, module->wa_module_vers);

    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    wa_needed = mm_arena_zalloc_simple(dl_arena, count * sizeof(struct wa_module_needed), NULL);
    if (wa_needed == NULL) {
        printf("%s failed to alloc wa_needed..\n", __func__);
    }

    dl_state->dl_modules_needed_count = count;
    dl_state->dl_modules_needed = wa_needed;

    for (int i = 0; i < count; i++) {
        uint8_t type, vers_type;
        uint32_t vers_count;
        type = *(ptr);
        ptr++;
        namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (namesz != 0) {
            name = mm_arena_alloc_simple(dl_arena, namesz + 1, NULL);
            strlcpy(name, (const char *)ptr, namesz + 1);
            ptr += namesz;
            wa_needed->wa_module_name = name;
        }
        vers_count = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (vers_count != 0) {
            vers_type = *(ptr);
            ptr++;
            if (vers_type == 1) {
                vers = NULL;
                verssz = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                if (verssz != 0) {
                    vers = mm_arena_alloc_simple(dl_arena, verssz + 1, NULL);
                    if (vers != NULL)
                        strlcpy(vers, (const char *)ptr, verssz + 1);
                    ptr += verssz;
                }
                wa_needed->wa_module_vers = vers;
            } else if (vers_type == 2) {
                vers = NULL;
                verssz = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                if (verssz != 0) {
                    ptr += verssz;
                }
                vers = NULL;
                verssz = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                if (verssz != 0) {
                    ptr += verssz;
                }
            }
        }

        printf("%s needed module name '%s' vers = '%s'\n", __func__, wa_needed->wa_module_name, wa_needed->wa_module_vers);
        wa_needed++;
    }
    // element segments
    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    if (count == 0) {
        return 0;
    }

    if (module->wa_elem_segments == NULL) {
        wa_elem = mm_arena_alloc_simple(dl_arena, sizeof(struct wa_elem_segment_info) * count, NULL);
        if (wa_elem == NULL) {
            printf("%s failed to allocate wa_elem_segments vector..\n", __func__);
            return ENOMEM;
        }
        module->wa_elem_segments = wa_elem;
    }

    module->wa_elem_count = count;

    for (int i = 0; i < count; i++) {
        uint8_t type, vers_type;
        uint32_t segidx, seg_align, seg_size, seg_dataSize;
        type = *(ptr);
        ptr++;
        namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;

        name = NULL;
        if (namesz != 0) {
            name = mm_arena_alloc_simple(dl_arena, namesz + 1, NULL);
            if (name != NULL)
                strlcpy(name, (const char *)ptr, namesz + 1);
            ptr += namesz;
        }
        segidx = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;

        seg_align = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        
        seg_size = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;

        seg_dataSize = 0; // Todo: enable this!

        wa_elem->wa_namesz = namesz;
        wa_elem->wa_name = name;
        wa_elem->wa_size = seg_size;
        wa_elem->wa_align = seg_align;
        wa_elem->wa_dataSize = seg_dataSize;

        dbg_loading("%s element-segment @%p type = %d name = %s (namesz = %d) size = %d (data-size: %d) align = %d\n", __func__, wa_elem, type, wa_elem->wa_name, namesz, wa_elem->wa_size, wa_elem->wa_dataSize, wa_elem->wa_align);

        wa_elem++;
    }

    // data segments
    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    if (count > 0) {

        arr = mm_arena_zalloc_simple(dl_arena, sizeof(struct wa_data_segment_info) * count, NULL);
        if (arr == NULL) {
            return ENOMEM;
        }
        seg = arr;
        data_sec = wasm_find_section(module, WASM_SECTION_DATA, NULL);
        // data_sec might be null if binary only uses .bss

        min_data_off = 1;                                   // TODO: + lebsz for count
        max_data_off = data_sec ? data_sec->wa_size : 0;    // TODO: - lebsz for count
        data_off_start = data_sec ? data_sec->wa_offset : 0;

        for(int i = 0; i < count; i++) {
            uint32_t dataoff, flags, max_align, size;
            dataoff = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
            flags = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
            max_align = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
            size = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
            namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            name = NULL;
            
            if (namesz != 0) {
                name = mm_arena_alloc_simple(dl_arena, namesz + 1, NULL);
                strlcpy(name, (const char *)ptr, namesz + 1);
                ptr += namesz;
            }
            if (dataoff >= min_data_off && dataoff < max_data_off) {
                dataoff += data_off_start;
            } else if (dataoff != 0) {
                printf("%s data-offset %d for data segment is out of range (min: %d max: %d) %d\n", __func__, dataoff, min_data_off, max_data_off);
                return EINVAL;
            }

            seg->wa_flags = flags;
            seg->wa_align = max_align;
            seg->wa_size = size;
            seg->wa_namesz = namesz;
            seg->wa_name = name;
            seg->wa_src_offset = dataoff;

            dbg_loading("%s index = %d name %s size = %d align = %d data-offset = %d\n", __func__, i, seg->wa_name, seg->wa_size, seg->wa_align, seg->wa_src_offset);
            seg++;
        }

        module->wa_data_count = count;
        module->wa_data_segments = arr;
    }

    // uleb count

    // u8 type
    // uleb name-sz 
    // bytes name
    // uleb count
    // u8 vers-type
    // if == 1
    // uleb name-sz bytes name
    // if == 2
    // min | uleb vers-sz bytes vers 
    // max | uleb vers-sz bytes vers

    // followed by element-segment metadata
    // count
    // u8 type
    // uleb name-sz
    // bytes name
    // uleb segidx
    // uleb max_align
    // uleb size

    // followed by data-segment metadata
    // count
    // uleb segidx
    // uleb flags
    // uleb max_align
    // uleb size
    // uleb name-sz
    // bytes name

    return 0;
}



#include <stdio.h>
#include <sys/stdint.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/exec.h>

#include <stddef.h>

#include "arch/wasm/libwasm/rtld.h"
#include "errno.h"
#include "kmem.h"
#include "libwasm.h"
#include "module.h"
#include "mutex.h"
#include "null.h"
#include "wasmloader.h"
#include "loader.h"
#include "kld_ioctl.h"

// FIXME: hacky path
#include <wasm/../mm/mm.h>
#include <wasm/../../../../libexec/ld.wasm/rtld_paths.h>

// breaking away from the initial concept for loading dynamic linked executable.
// from the begining it was implemented enterly in the kernel, using kernel memory..
// 
// since every dynamic linked executable is going to need rtld awyways, the new concept
// is to simply reloc the rtld which will take over the loading of everything else.

static const char __rtldmodule_path[] = "/libexec/ld-wasm.so.1";

#define RTLD_CACHE_UNINIT 0
#define RTLD_CACHE_LOADED 1
#define RTLD_CACHE_FALURE 3

static struct {
    uint32_t state;                     // state of the cache.
    kmutex_t lock;
    struct wash_exechdr_rt *exechdr;
    struct wasm_module_rt *module;
    struct {
        uint8_t *min_leb_p;     // pointer to the start of the uleb which stores memory.initial
        uint8_t min_lebsz;
        uint8_t max_lebsz;
    } mem;
    uint8_t *exec_data;         // memory cache of the entire binary of rtld
} rtld_cache;

int _rtld_find_import_memory(uint8_t *data_start, uint8_t *data_end, uint32_t off, struct wasm_loader_meminfo *meminfo);

int load_ldwasm_read_dylink0(struct wasm_module_rt *, struct wash_exechdr_rt *, uint8_t *);

int
load_rtld_module_from_disk(void)
{
    struct wasm_exechdr_secinfo *sec;
    struct wash_exechdr_rt *exechdr;
    struct wasm_module_rt *module;
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
    uint8_t *modbufp;
    uint8_t *exechdr_p;
    union {
        struct wasm_loader_cmd_mkbuf mkbuf;
        struct wasm_loader_cmd_wrbuf wrbuf;
        struct wasm_loader_cmd_compile ccbuf;
        struct wasm_loader_cmd_run run;
        struct wasm_loader_cmd_cp_kmem_to_umem cp_kmem;
    } exec_cmd;
    uint32_t bufsz;
    uint32_t pgcnt;
    uint32_t count;
    size_t exechdr_off, exechdr_size;
	int error;

    mutex_spin_enter(&rtld_cache.lock);

    if (rtld_cache.state == RTLD_CACHE_FALURE) {
        mutex_spin_exit(&rtld_cache.lock);
        return ENOEXEC;
    } else if (rtld_cache.state != RTLD_CACHE_UNINIT) {
        mutex_spin_exit(&rtld_cache.lock);
        return EBUSY;
    }

    module = NULL;

	l = (struct lwp *)curlwp;
    path = PNBUF_GET();
    pathlen = strlen(__rtldmodule_path);
    strlcpy(path, __rtldmodule_path, pathlen + 1);
    pb = pathbuf_assimilate(path);
	NDINIT(&nd, LOOKUP, FOLLOW | TRYEMULROOT, pb);

	/* first get the vnode */
	if ((error = namei(&nd)) != 0) {
        pathbuf_destroy(pb);
        rtld_cache.state = RTLD_CACHE_FALURE;
        mutex_spin_exit(&rtld_cache.lock);
        dbg_loading("error: could not find %s error = %d\n", __rtldmodule_path, error);
		return error;
    }

	vp = nd.ni_vp;

    vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

	/* XXX VOP_GETATTR is the only thing that needs LK_EXCLUSIVE here */
	if ((error = VOP_GETATTR(vp, &vap, l->l_cred)) != 0)
		goto loading_failure;

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
        error = ENOMEM;
        goto loading_failure;
    }

    modbufp = (uint8_t *)modbuf;
    rtld_cache.exec_data = (uint8_t *)modbufp;

    // TODO: when trying to use a dynld which uses reloc, try to alloc pages to cover it within kernel memory
    //       and perform all ops on that memory, and once its ready read in the essential section into execbuf.


    off = 0;
    while (off < fsize) {
        fbufsz = MIN(bsize, fsize - off);
        exec_read((struct lwp *)curlwp, vp, off, buf, fbufsz, IO_NODELOCKED);

        memcpy(modbufp, buf, fbufsz);
        modbufp += fbufsz;

        // TODO: make assertion based upon module section content.
        off += fbufsz;
    }

    VOP_UNLOCK(vp);

    if (wasm_loader_has_exechdr_in_buf(modbufp, fsize, &exechdr_off, &exechdr_size) == false) {
        printf("%s missing netbsd.exec-hdr as first section\n", __func__);
        goto loading_failure;;
    }

    exechdr = (struct wash_exechdr_rt *)(modbufp + exechdr_off);
    if (exechdr->hdr_version != 1) {
        printf("%s unsupported netbsd.exec-hdr version; %d\n", __func__, exechdr->hdr_version);
        goto loading_failure;
    }

    rtld_reloc_exechdr(modbufp + exechdr_off);


    printf("%s modbuf = %p buf = %p\n", __func__, modbuf, buf);

    // 2. find memory import
    sec = _rtld_exechdr_find_section(exechdr, WASM_SECTION_CUSTOM, "netbsd.dylink.0");
    if (sec == NULL) {
        printf("%s no import memory\n", __func__);
        goto loading_failure;
    }

    error = _rtld_find_import_memory(modbufp + sec->file_offset, modbufp + (sec->file_offset + sec->sec_size), sec->file_offset, &meminfo);
    if (error != 0) {
        printf("%s got error = %d from rtld_find_module_memory()\n", __func__, error);
        return EINVAL;
    }
    printf("%s meminfo min = %d max = %d shared = %d min_file_offset = %d min_lebsz = %d max_lebsz %d\n", __func__, meminfo.min, meminfo.max, meminfo.shared, meminfo.min_file_offset, meminfo.min_lebsz, meminfo.max_lebsz);

    rtld_cache.mem.min_leb_p = modbufp + meminfo.min_file_offset;
    rtld_cache.mem.min_lebsz = meminfo.min_lebsz;
    rtld_cache.mem.max_lebsz = meminfo.max_lebsz;

#if 0
    // 3. find data-segment info (just offset + size)
    sec = wasm_find_section(wa_mod, WASM_SECTION_DATA, NULL);
    if (sec) {
        rtld_read_data_segments_info(dlctx, wa_mod, sec);
    }
#endif

    // 4. find dylink.0 section
    sec = _rtld_exechdr_find_section(exechdr, WASM_SECTION_CUSTOM, "netbsd.dylink.0");
    if (sec) {
        rtld_read_dylink0_subsection_info(dlctx, wa_mod, sec);
    } else {
        printf("%s missing dylink.0 section.. cannot link!", __func__);
        error = ENOEXEC;
        goto loading_failure;
    }

    module = kmem_zalloc(sizeof(struct wasm_module_rt), 0);
    if (module == NULL) {
        error = ENOMEM;
        goto loading_failure;
    }

    load_ldwasm_read_dylink0(module, exechdr, modbufp);

    rtld_dylink0_decode_modules(dlctx, wa_mod, wa_mod->wa_dylink_data);



    if (modbuf != NULL) {
        kmem_page_free(modbuf, pgcnt);
    }

    mm_arena_free_simple(dl_arena, errstr);

    pathbuf_destroy(pb);    // pathbuf calls PNBUF_PUT on path

    vput(vp);

	return 0;

loading_failure:

    rtld_cache.state = RTLD_CACHE_FALURE;
    mutex_spin_exit(&rtld_cache.lock);

    pathbuf_destroy(pb);    // pathbuf calls PNBUF_PUT on path

    return error;
}

int
load_ldwasm_read_dylink0(struct wasm_module_rt *module, struct wash_exechdr_rt *hdr, uint8_t *data_start)
{

}

int
reloc_ldwasm_place_segments(struct wasm_module_rt *module, uintptr_t *data_base, uintptr_t *ftbl_base)
{

}

/**
 * 
 */
void *
reloc_ldwasm_find_symbol(struct wasm_module_rt *module, uint8_t *data_start, const char *name, uint32_t namesz)
{

}

int
reloc_ldwasm_module(uintptr_t *data_base, int32_t memory_min, int32_t memory_max, uintptr_t *ftbl_base)
{
    struct wasm_module_rt *objself;
    union {
        struct wasm_loader_cmd_mkbuf mkbuf;
        struct wasm_loader_cmd_wrbuf wrbuf;
        struct wasm_loader_cmd_run run;
        struct wasm_loader_cmd_cp_kmem_to_umem cp_kmem;
    } exec_cmd;
    uint8_t *ptr;
    int execfd; // not to be confused with the execfd sent as aux param.
    int error;

    if (memory_min == -1 || memory_max == -1) {
        printf("%s initial %d or maximum %d memory are unset, which is currently not supported..\n", __func__, memory_min, memory_max);
        return ENOEXEC;
    }
    
    if (rtld_cache.mem.min_leb_p != NULL) {
        ptr = rtld_cache.mem.min_leb_p;
        encodeULEB128(memory_min, ptr, rtld_cache.mem.min_lebsz);
        ptr += rtld_cache.mem.min_lebsz;
        encodeULEB128(memory_max, ptr, rtld_cache.mem.max_lebsz);
    }

    error = reloc_ldwasm_place_segments(rtld_cache.module, data_base, ftbl_base);
    if (error != 0) {
        printf("%s got error = %d after wasm_loader_dynld_do_internal_reloc()\n", __func__, error);
    }

    error = rtld_do_extern_reloc(l, dlctx, wa_mod);
    if (error != 0) {
        printf("%s got error = %d after wasm_loader_dynld_do_extern_reloc()\n", __func__, error);
    }

    exec_cmd.mkbuf.buffer = -1;
    exec_cmd.mkbuf.size = fsize; // TODO: use exec_end_offset later on, but first we must get it up and running..
    error = wasm_exec_ioctl(EXEC_IOCTL_MKBUF, &exec_cmd);
    execfd = exec_cmd.mkbuf.buffer;

    exec_cmd.wrbuf.buffer = execfd;
    exec_cmd.wrbuf.offset = 0;
    exec_cmd.wrbuf.size = fsize;
    exec_cmd.wrbuf.src = modbuf;
    error = wasm_exec_ioctl(EXEC_IOCTL_WRBUF, &exec_cmd);


    // before sending the executable of to new WebAssembly.Module
    // there is some internal state which the exec needs to share with 
    // the rtld module which will take over the loading from here.
    // 
    // TODO: to setup the state use .dynsym along with .dynstr to do lookup
    // for a specific symbol (convert reloc offsets to kernel offsets)
    // copy the memory of the symbol, apply changes and put modified memory back.
    // 
    // setup state on __rtld_state
    // setup of rtld object 

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
    static const char sym__rtld_objself = "__rtld_objself";
    rtld_state = reloc_ldwasm_find_symbol(module, rtld_cache.exec_data, sym__rtld_objself, sizeof(sym__rtld_objself));
    // TODO: we need set certain properties on __rtld_objself but it much easier todo so when memory have been copied
    //       and apply such changes there, doing it before requires a translation back to where it originates in the file..
    // needs to specify:
    // objself->data_segments
    // objself->elem_segments
    // objself->filepath
    // etc..


    error = wasm_exec_ioctl(EXEC_IOCTL_RUN_RTLD_INIT, NULL);
    if (error != 0) {
        printf("%s got error = %d from running _rtld_init\n", __func__, error);
    }

    vput(vp);

	return 0;

loading_failure:

    rtld_cache.state = RTLD_CACHE_FALURE;
    mutex_spin_exit(&rtld_cache.lock);

    pathbuf_destroy(pb);    // pathbuf calls PNBUF_PUT on path

    return error;
}


int
setup_ldwasm_module_from_cache(uintptr_t *relocbase, int32_t mem_min, int32_t mem_max)
{
    int error;

    error = 0;

    mutex_spin_enter(&rtld_cache.lock);
    if (rtld_cache.state == RTLD_CACHE_UNINIT) {
        error = load_rtld_module_from_disk();
    }

    if (error) {
        mutex_spin_exit(&rtld_cache.lock);
        return error;
    }

    error = reloc_ldwasm_module(relocbase, mem_min, mem_max, 1);
}

/**
 * In-memory variant of 
 * @param 
 */
int
_rtld_find_import_memory(uint8_t *data_start, uint8_t *data_end, uint32_t off, struct wasm_loader_meminfo *meminfo)
{   
    const char *module_name;
    const char *name;
    struct wa_section_info *sec;
    uint32_t module_namesz, namesz, count, lebsz, secsz;
    uint8_t kind;
    uint8_t *ptr, *ptr_start, *end, *file_start, *sym_start;
    const char *errstr = NULL;

    ptr = data_start;
    end = data_end;
    errstr = NULL;

    if (*(ptr) != WASM_SECTION_IMPORT) {
        printf("%s buffer start is not import section %02x found\n", __func__, *(ptr));
        return ENOEXEC;
    }
    ptr++;

    secsz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    if (ptr + secsz != data_end) {
        printf("%s section-size missmatch %d vs %d\n", __func__, secsz, data_end - ptr);
        return ENOEXEC;
    }

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
            min_file_offset = (ptr - data_start) + off;
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

    return ENOENT;
}
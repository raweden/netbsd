/*
 * Copyright (c) 2024 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Raweden @github 2024.
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
 */

#include <sys/errno.h>
#include <sys/stdint.h>
#include <sys/stdbool.h>
#include <sys/null.h>

#include <dlfcn.h>

#include "stdlib.h"
#include "rtld.h"

#include <arch/wasm/libwasm/libwasm.h>
#include <arch/wasm/libwasm/wasmloader.h>
#include <arch/wasm/include/wasm_inst.h>
#include <arch/wasm/include/wasm-extra.h>
#include <arch/wasm/include/wasm/builtin.h>
#include <arch/wasm/include/wasm_module.h>

#include "rtsys.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

// TODO: implement a rtld version of malloc/free
// TODO: rtld will need a mmap() like function to map/unmap page chunks,

// used as a limit for shared-object name & version strings
// the current behivor is that the linker fails if given input above these limits.
#define NAME_MAX 128
#define VERS_MAX 32

#define DEBUG_RTLD 1

#ifdef DEBUG_RTLD
void _rtld_debug_printf(const char *format, ...) __attribute__((__format__ (__printf__, 1, 2)));
#define dbg(...) _rtld_debug_printf(__VA_ARGS__)
#else
#define dbg(...)
#endif

#ifdef DEBUG_LOADING
void _rtld_debug_printf(const char *format, ...) __attribute__((__format__ (__printf__, 1, 2)));
#define dbg_loading(...) _rtld_debug_printf(__VA_ARGS__)
#else
#define dbg_loading(...)
#endif

#ifndef RTLD_DEFAULT_LIBRARY_PATH
#define RTLD_DEFAULT_LIBRARY_PATH "/usr/lib"
#endif

// _rtld_exec_ioctl is the main interface from the runtime-loader/linker to the hosting
// WebAssembly virtual-machine, which requires to step into JavaScript land.

int _rtld_exec_ioctl(int cmd, uintptr_t argp) __WASM_IMPORT(rtld, rtld_exec_ioctl);

#define RTLD_IOCTL_EXECBUF_MAP_FROM_MEMORY 569

struct execbuf_map_param {
    uint32_t dst;
    uint32_t src;
    uint32_t len;
};

// 
struct rtld_cmd_map_execbuf {
    int exec_fd;
    uint32_t buf_size;
    uint32_t map_count;
    struct buf_remap_param *maplist;
};

struct rtld_cmd_compile_v2 {
    int32_t buffer;             // exec buffer identifier (exec_fd)
    int32_t flags;
    uintptr_t __dso_handle;     // provided as dso_self on the __dsym, __dlopen calls.
    uintptr_t __tls_handle;
    char *export_name;          // dso defines regular exports required for subsequent loading (set to NULL if not used)
    uint8_t export_namesz;      // length of export_name if non-NULL.
    uint8_t errphase;
    int32_t errno;
    uint32_t errmsgsz;
    char *errmsg;
};

// wasm builtins; replaced by the linker
void wasm_table_get(int tableidx, int index) __WASM_BUILTIN(table_get);
void wasm_table_set(int tableidx, int index) __WASM_BUILTIN(table_get);

struct rtld_state {
    struct rtld_state_common rtld;
    // for dynamic loading after entering main() and when memory.grow is not a option.
    void *(*libc_malloc)(unsigned long);
    void (*libc_free)(void *);
    // in order to load & link the runtime-linker needs syscall access, this before libc is loaded (rtld calls syscall using the trap itself.)
    int (*__sys_open)(const char *filepath, int flags, long arg);
    int (*__sys_close)(int fd);
    int64_t (*__sys_lseek)(int fd, int64_t offset, int whence);
    int (*__sys_getdents)(int fd, char *buf, unsigned long count);
    long (*__sys_read)(int fd, void *buf, unsigned long nbyte);
    long (*__sys_write)(int fd, const void *buf, unsigned long nbyte);
    int (*__sys_fstat)(int fd, struct stat *sb);
    int (*__sys_lstat)(const char *path, struct stat *ub);
    ssize_t (*__sys_readlink)(const char *path, char *buf, size_t count);
    int (*__sys_fcntl)(int fd, int cmd, ...);   // damn what these dots always causes trouble & bugs...
};

#define STATIC_DEFINED_PATH(x) { .sp_pathlen = (sizeof((x)) - 1), .sp_path = (x)};

struct _rtld_search_path _rtld_usr_lib = STATIC_DEFINED_PATH(RTLD_DEFAULT_LIBRARY_PATH);

#define RTLD_STATE_UNINIT 0
#define RTLD_STATE_INIT 1

struct rtld_state __rtld_state;
int errno;

// rtld
struct wasm_module_rt *_rtld_load_library(struct wasm_module_rt *, const char *, struct wasm_module_rt *, int);
int _rtld_unload_object(struct wasm_module_rt *);
void _rtld_error(const char *format, ...) __attribute__((__format__ (__printf__, 1, 2)));
struct wasm_module_rt* _rtld_load_object(const char *name, int flags);
struct wasm_module_rt *_rtld_find_dso_handle_by_name(const char *name, const char *vers);
int _rtld_load_needed_objects(struct wasm_module_rt *obj, int flags);
int _rtld_link_against_libc(struct wasm_module_rt *libc_dso);
int __dlsym_early(struct dlsym_rt * restrict start, struct dlsym_rt * restrict end, unsigned int namesz, const char * restrict name, unsigned char type);
void _rtld_add_paths(const char *execname, struct _rtld_search_path **phead, const char *path);

// rtld exex_cmd

#define EXEC_IOCTL_COMPILE 557
#define EXEC_IOCTL_MKBUF 552
#define EXEC_IOCTL_WRBUF 553
#define EXEC_IOCTL_COREDUMP 580

int rtld_exec_ioctl(int cmd, void *arg) __WASM_IMPORT(rtld, exec_ioctl);

struct wasm_loader_cmd_mkbuf {
    int32_t objdesc;
    uint32_t size;
};

struct wasm_loader_cmd_wrbuf {
    int32_t objdesc;
    void *src;
    uint32_t offset;
    uint32_t size;
};

struct wasm_loader_cmd_compile_v2 {
    int32_t objdesc;
    int32_t flags;
    uintptr_t __dso_handle;
    uintptr_t __tls_handle;
    char *export_name;
    uint8_t export_namesz;
    uint8_t errphase;
    int32_t errno;
    uint32_t errmsgsz;
    char *errmsg;
};

struct wasm_loader_cmd_coredump {
    int32_t objdesc;
    const char *filename;
    uint32_t offset;
    uint32_t size;
};

//

void _rtld_mutex_enter(uint32_t *ld_mutex)
{
    uint32_t cnt;
    uint32_t val;
    cnt = 0;
    val = __builtin_atomic_rmw_cmpxchg32(ld_mutex, 0, 1);
    while (val != 0) {
        if (cnt >= 10000) {
            __panic_abort();
        }
        val = __builtin_atomic_rmw_cmpxchg32(ld_mutex, 0, 1);
    }
}

int _rtld_mutex_try_enter(uint32_t *ld_mutex)
{
    uint32_t val;
    val = __builtin_atomic_rmw_cmpxchg32(ld_mutex, 0, 1);
    if (val == 0) {
        return true;
    }

    return false;
}

void _rtld_mutex_exit(uint32_t *ld_mutex)
{
    uint32_t val;
    val = __builtin_atomic_rmw_cmpxchg32(ld_mutex, 1, 0);
    if (val != 1) {
        __panic_abort();
    }
}

#define _RTLD_UNLOADING (1 << 4)
#define _RTLD_OBJ_MALLOC (1 << 3)

#define _RTLD_SEGMENT_COMMON_NAME (1 << 2)

/*
 * Program Header
 */
typedef struct {
	uint32_t	p_type;		/* entry type */
	uint32_t	p_offset;	/* offset */
	uintptr_t	p_vaddr;	/* virtual address */
	uintptr_t	p_paddr;	/* physical address */
	uint32_t	p_filesz;	/* file size */
	uint32_t	p_memsz;	/* memory size */
	uint32_t	p_flags;	/* flags */
	uint32_t	p_align;	/* memory & file alignment */
} Elf32_Phdr;

struct dl_phdr_info {
    uintptr_t dlpi_addr;    /* Base address of object */
    const char *dlpi_name;  /* (Null-terminated) name of object */
    const Elf32_Phdr *dlpi_phdr;  /* Pointer to array of ELF program headers for this object */
    uint16_t        dlpi_phnum; /* # of items in dlpi_phdr */
};

#define RTLD_PRE_INIT_ARR_DONE 0x010000
#define RTLD_INIT_ARR_DONE 0x020000
#define RTLD_FNIT_ARR_SETUP_DONE 0x040000

__attribute__((constructor))
void
__rtld_init(void) 
{
    struct wasm_module_rt *libc_dso;

    __rtld_state.rtld.ld_state = RTLD_STATE_INIT;
    __rtld_state.__sys_open = __sys_open;
    __rtld_state.__sys_close = __sys_close;
    __rtld_state.__sys_lseek = __sys_lseek;
    __rtld_state.__sys_getdents = __sys_getdents;
    __rtld_state.__sys_read = __sys_read;
    __rtld_state.__sys_write = __sys_write;
    __rtld_state.__sys_fstat = __sys_fstat;
    __rtld_state.__sys_lstat = __sys_lstat;
    __rtld_state.__sys_readlink = __sys_readlink;
    __rtld_state.__sys_fcntl = __sys_fcntl;

    // libc is needed for memory malloc/free (unlike elf rtld which likley uses mmap for this)
    libc_dso = _rtld_find_dso_handle_by_name("libc", NULL);
    if (libc_dso != NULL) {
        _rtld_link_against_libc(libc_dso);
    }

    // TODO: rtld must declare malloc of it own to use this before linking is fully done in kernel based linking
    //_rtld_add_paths(NULL, &__rtld_state.rtld.ld_default_paths, RTLD_DEFAULT_LIBRARY_PATH);

    __rtld_state.rtld.ld_default_paths = &_rtld_usr_lib;
}

#if 0
__attribute__((destructor))
void 
__rtld_fnit(void) 
{

}
#endif

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

int 
_rtld_link_against_libc(struct wasm_module_rt *libc_dso)
{
    __rtld_state.libc_free = (void (*)(void *))__dlsym_early((struct dlsym_rt *)libc_dso->dlsym_start, (struct dlsym_rt *)libc_dso->dlsym_end, 4, "free", 1);
    __rtld_state.libc_malloc = (void* (*)(unsigned long))__dlsym_early((struct dlsym_rt *)libc_dso->dlsym_start, (struct dlsym_rt *)libc_dso->dlsym_end, 6, "malloc", 1);


    dbg("%s linking for libc_free %p", __func__, __rtld_state.libc_free);
    dbg("%s linking for libc_malloc %p", __func__, __rtld_state.libc_malloc);

    return (0);
}

// rtld debug

#ifdef DEBUG_RTLD
void _rtld_write_log(const char *buf, unsigned int bufsz, unsigned int flags, unsigned int level) __WASM_IMPORT(rtld, cons_write);

void _rtld_debug_printf(const char *fmt, ...)
{
    static char     buf[512];
	va_list         ap;
    int logsz;

	va_start(ap, fmt);
	logsz = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
    _rtld_write_log(buf, logsz, (1 << 3), 3);
}
#endif

// 

// all user supplied handles are piped trough this function which allows for checking if the handle is valid.
struct wasm_module_rt *
_rtld_find_dso_handle(void *handle)
{
    struct wasm_module_rt *obj;

    for (obj = __rtld_state.rtld.objlist; obj != NULL; obj = obj->next) {
        if (obj == (struct wasm_module_rt *)handle) {
            return obj;
        }
    }

    return NULL;
}

struct wasm_module_rt *
_rtld_find_dso_handle_by_name(const char *name, const char *vers)
{
    struct wasm_module_rt *obj;
    uint16_t namesz;
    uint16_t verssz;

    namesz = strnlen(name, NAME_MAX);
    verssz = vers != NULL ? strnlen(vers, NAME_MAX) : 0;

    for (obj = __rtld_state.rtld.objlist; obj != NULL; obj = obj->next) {
        if (obj->dso_namesz == namesz && strncmp(obj->dso_name, name, namesz) == 0 && (verssz == 0 || strncmp(obj->dso_vers, vers, verssz) == 0)) {
            return obj;
        }
    }

    return NULL;
}

#if 0
int
_rtld_find_dso_path(const char *filepath, const char *pathbuf, uint32_t *pathsz)
{
    struct stat st_buf;
    char *buf;
    uint32_t dylink0_off;
    uint32_t filesz, bufsz;
    int fd, ret, error;

    // check pwd (current working directory)

    fd = __rtld_state.__sys_open(".", FREAD | O_DIRECTORY, 0);
    if (fd == -1) {
        error = errno;
        return error;
    }

    // Implement something similar to Elf's DT_RPATH

    // LD_LIBRARY_PATH

    // lib & usr/lib

    return (0);
}
#endif

int
_rtld_unload_object(struct wasm_module_rt *dso)
{
    if (dso->ld_refcount == 0) {
        dso->ld_state = _RTLD_UNLOADING;
    }

    return (0);
}

void
_rtld_call_init_functions(struct wasm_module_rt *first)
{
    struct wasm_module_rt *obj;
    uint32_t cnt;
    uint32_t *fp;
    void (*init_fn)(void);

    dbg("%s calling pre_init_array & init_array", __func__);

    for (obj = first; obj != NULL; obj = obj->next) {

        if ((obj->flags & RTLD_PRE_INIT_ARR_DONE) == 0) {
            if (obj->pre_init_array != NULL && obj->pre_init_array_count != 0) {
                cnt = obj->pre_init_array_count;
                fp = (uint32_t *)(obj->pre_init_array);
                for (int i = 0; i < cnt; i++) {
                    init_fn = (void (*)(void))(*(fp));
                    init_fn();
                    fp++;
                }
            }
            obj->flags |= RTLD_PRE_INIT_ARR_DONE;

        }

        if ((obj->flags & RTLD_INIT_ARR_DONE) == 0) {
            if (obj->init_array != NULL && obj->init_array_count != 0) {
                cnt = obj->init_array_count;
                fp = (uint32_t *)(obj->init_array);
                for (int i = 0; i < cnt; i++) {
                    init_fn = (void (*)(void))(*(fp));
                    init_fn();
                    fp++;
                }
            }
            obj->flags |= RTLD_INIT_ARR_DONE;
        }

    }
}


int
__dlsym_early(struct dlsym_rt * restrict start, struct dlsym_rt * restrict end, unsigned int namesz, const char * restrict name, unsigned char type)
{
    struct dlsym_rt *ptr = start;
    while (ptr < end) {
        if (type == ptr->type && namesz == ptr->namesz && strncmp(name, ptr->name, namesz) == 0) {
            return (int)ptr->addr;
        }

        ptr++;
    }

    return -(ENOENT);
}

struct dlsym_rt *
__dlsym_internal(struct dlsym_rt * restrict start, struct dlsym_rt * restrict end, unsigned int namesz, const char * restrict name, unsigned char type)
{
    struct dlsym_rt *ptr = start;
    while (ptr < end) {
        if (type == ptr->type && namesz == ptr->namesz && strncmp(name, ptr->name, namesz) == 0) {
            return ptr;
        }

        ptr++;
    }

    return NULL;
}

// the dynld is provided with the self dso-handle trough the use of a Wasm-Global in each shared-object.
// this is a requirement for the RTLD_SELF to work.

void *
__dlauxinfo(struct wasm_module_rt *dso_self)
{
    return NULL;
}

/**
 * Backend for `dlopen()` 
 */
void *
__dlopen(struct wasm_module_rt *dso_self, const char *filepath, int flags)
{
    struct wasm_module_rt *mod, *dso, *oldtail;
    bool nodelete;
    bool isfilename;
    char *pathbuf;
    struct stat sbuf;
    int error;

    if (__rtld_state.rtld.ld_state == RTLD_STATE_UNINIT) {
        __rtld_init();
    }

    oldtail = __rtld_state.rtld.objtail;

    if (oldtail->next != NULL) {
        dbg("%s old tail is not null %p", __func__, oldtail->next);
    }

    // return handle for the main program
    if (filepath == NULL) {
        dso = __rtld_state.rtld.objmain;
        dso->ld_refcount++;
    } else {
        dso = _rtld_load_library(dso_self, filepath, __rtld_state.rtld.objmain, flags);
        dbg("%s got %p from _rtld_load_library()", __func__, dso);
    }

    if (dso != NULL) {
        dso->ld_refcount++;
        if (oldtail->next != NULL) {
            dbg("%s oldtail->next is not NULL", __func__);

            error = _rtld_load_needed_objects(dso, flags);
            if (error == 0) {
                dbg("%s got no error call ctors", __func__);
                _rtld_call_init_functions(dso);
            } else {
                dbg("%s got error %d from _rtld_load_needed_objects()", __func__, error);
            }
        } else {
            dbg("%s oldtail->next is NULL", __func__);
        }

    }

    return dso;
#if 0
    nodelete = (flags & RTLD_NODELETE) != 0;
    isfilename = strchr(filepath, '/') == NULL;

    // check if dso is already loaded.
    dso_match = NULL;
        
    // TODO: should check path since we might have been provided with a symbolic link.
    struct wasm_module_rt **vec = (struct wasm_module_rt **)__rtld_state.dsovec;
    while (*(vec) != NULL) {
        mod = *(vec);
        if (mod->filepath == NULL) {
            vec++;
            continue;
        }

        if (isfilename) {
            char *last = strrchr(mod->filepath, '/');
            if (last != NULL && strcmp(last, filepath) == 0) {
                dso_match = mod;
                break;
            }
        } else if (strcmp(mod->filepath, filepath) == 0) {
            dso_match = mod;
            break;
        }
        vec++;
    }


    if ((flags & RTLD_NOLOAD) != 0) {
        return dso_match;
    }

    error = _rtld_do_substitution(dso_self, filepath, pathbuf);

    error = _rtld_find_dso_path(filepath, NULL, NULL);
    if (error != 0) {

    }

    return NULL;
#endif
}

// TODO: check libexec/ld.elf_so/rtld.c to be constistent with error codes.
int
__dlclose(struct wasm_module_rt *dso_self, void *handle)
{
    struct wasm_module_rt *module;

    module = _rtld_find_dso_handle(handle);
    if (module != NULL) {

        // 
        if ((module->flags & RTLD_NODELETE) != 0) {
            return -1;
        }

        module->ld_refcount--;
        if (module->ld_refcount == 0) {
            _rtld_unload_object(module);
        }

    }    

    return ENOENT;
}

/**
 * WebAssembly note: since function-pointer and data-pointers have overlapping ranges (separate address space) the return cannot be determined to be of a certain type.
 */
void *
__dlsym(struct wasm_module_rt *dso_self, void * __restrict handle, const char * __restrict symbol)
{
    struct wasm_module_rt *obj;

    if (handle == RTLD_NEXT) {
        obj = _rtld_find_dso_handle(dso_self);
        if (obj == NULL) {
            return NULL;
        }
        obj = obj->next;
    } else if (handle == RTLD_SELF) {
        obj = _rtld_find_dso_handle(dso_self);
    } else if (handle == RTLD_DEFAULT) {
        obj = __rtld_state.rtld.objlist;
    }

    for (obj = __rtld_state.rtld.objlist; obj != NULL; obj = obj->next) {
        
    }

    return NULL;
}

/**
 * WebAssembly special (since function-pointers does not share address space with data-segments)
 */
void *
__dlsym_np(struct wasm_module_rt *dso_self, void * __restrict handle, const char * __restrict symbol, int symbol_type)
{
    if (handle == RTLD_NEXT) {

    } else if (handle == RTLD_SELF) {
        
    } else if (handle == RTLD_DEFAULT) {
        
    }

    return NULL;
}

int
__dladdr(struct wasm_module_rt *dso_self, const void * __restrict addr, Dl_info * __restrict dli)
{
    struct wasm_module_rt *obj;
    struct rtld_segment *segments, *segment;
    uintptr_t ptr;
    int count;
    bool found;

    uint16_t data_segments_count;
    uint16_t elem_segments_count;

    ptr = (uintptr_t)addr;

    for (obj = __rtld_state.rtld.objlist; obj != NULL; obj = obj->next) {
        count = obj->data_segments_count;
        segments = obj->data_segments;
        dbg("%s checking for %p in '%s'", __func__, addr, obj->filepath);
        for (int i = 0; i < count; i++) {
            segment = &segments[i];
            dbg("%s start = %p end = %p", segment->name, (void *)segment->addr, (void *)(segment->addr + segment->size));
            if (ptr >= segment->addr && ptr < (segment->addr + segment->size)) {
                dli->dli_fname = obj->filepath;
                dli->dli_fbase = (void *)segment->addr;
                dli->dli_saddr = NULL;
                dli->dli_sname = NULL;
                return (1);
            }
        }
    }

    return (0);
}

/**
 * WebAssembly special (since function-pointers does not share address space with data-segments)
 */
int
__dlfunc(struct wasm_module_rt *dso_self, const void * __restrict addr, Dl_info * __restrict dli)
{
    return (0);
}


int
__dlctl(struct wasm_module_rt *dso_self, void *handle, int cmd, void *data)
{

    switch (cmd) {
        case DL_GETERRNO:

            break;
        case DL_GETSYMBOL:

            break;
    }

    return (0);
}

// https://man7.org/linux/man-pages/man3/dlinfo.3.html
int
__dlinfo(struct wasm_module_rt *dso_self, void *handle, int request, void *info)
{
    switch (request) {
        case RTLD_DI_LINKMAP:

        //case RTLD_DI_ORIGIN:
        // RTLD_DI_SERINFO
        // RTLD_DI_SERINFOSIZE
        // RTLD_DI_LMID

            break;
        case DL_GETSYMBOL:

            break;
    }

    return (0);
}

void *
__dlvsym(struct wasm_module_rt *dso_self, void * __restrict handle, const char * __restrict symbol, const char * __restrict version)
{
    return NULL;
}

// no dso_self handle
void
__dl_cxa_refcount(void *handle, ssize_t delta)
{

}

// dlerror

const char *
__dlerror(struct wasm_module_rt *dso_self)
{
    const char *msg;
    msg = __rtld_state.rtld.error_message;
    __rtld_state.rtld.error_message = NULL;
    return msg;
}

/*
 * Error reporting function.  Use it like printf.  If formats the message
 * into a buffer, and sets things up so that the next call to dlerror()
 * will return the message.
 */
void
_rtld_error(const char *fmt, ...)
{
	static char     buf[512];
	va_list         ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	dbg("%s: %s", __func__, buf);
	__rtld_state.rtld.error_message = buf;
	va_end(ap);
}



int
__dl_iterate_phdr(struct wasm_module_rt *dso_self, int (*callback)(struct dl_phdr_info *, unsigned long, void *), void *data)
{
#if 0
	static bool setup_done;
	struct dl_phdr_info phdr_info;

	if (!setup_done) {

		setup_done = true;
	}

	wasm_memory_fill(&phdr_info, 0, sizeof(phdr_info));
	phdr_info.dlpi_addr = dlpi_addr;
	phdr_info.dlpi_phdr = dlpi_phdr;
	phdr_info.dlpi_phnum = dlpi_phnum;
	phdr_info.dlpi_name = dlpi_name;

	return callback(&phdr_info, sizeof(phdr_info), data);
#endif
    return (0);
}

int
call_pre_init_array(void)
{
    struct wasm_module_rt *obj;
    uint32_t cnt;
    uint32_t *fp;
    void (*pre_ctor)(void);

    for (obj = __rtld_state.rtld.objlist; obj != NULL; obj = obj->next) {
        if ((obj->flags & RTLD_PRE_INIT_ARR_DONE) != 0) {
            continue;
        }
        if (obj->pre_init_array != NULL && obj->pre_init_array_count != 0) {
            cnt = obj->pre_init_array_count;
            fp = (uint32_t *)(obj->pre_init_array);
            for (int i = 0; i < cnt; i++) {
                pre_ctor = (void (*)(void))(*(fp));
                pre_ctor();
                fp++;
            }
        }
        obj->flags |= RTLD_PRE_INIT_ARR_DONE;
    }

    return (0);
}

int
call_init_array(void)
{
    struct wasm_module_rt *obj;
    uint32_t cnt;
    uint32_t *fp;
    void (*ctor)(void);

    for (obj = __rtld_state.rtld.objlist; obj != NULL; obj = obj->next) {
        if ((obj->flags & RTLD_INIT_ARR_DONE) != 0) {
            continue;
        }
        if (obj->init_array != NULL && obj->init_array_count != 0) {
            cnt = obj->init_array_count;
            fp = (uint32_t *)(obj->init_array);
            for (int i = 0; i < cnt; i++) {
                ctor = (void (*)(void))(*(fp));
                ctor();
                fp++;
            }
        }
        obj->flags |= RTLD_INIT_ARR_DONE;
    }

    return (0);
}

int
setup_fnit_array(void)
{
    return (0);
}

int
__rtld(uintptr_t *sp, uintptr_t base)
{

    return (0);
}

int
__dyn_call_start(struct wasm_module_rt *dso_self, int (*__start)(void (*)(void), struct ps_strings *), struct ps_strings *psarg)
{

    return __start(NULL, psarg);
}

// expand

static const struct {
	const char *name;
	size_t namelen;
} bltn[] = {
#define ADD(a)	{ #a, sizeof(#a) - 1 },
	ADD(HWCAP)	/* SSE, MMX, etc */
	ADD(ISALIST)	/* XXX */
	ADD(ORIGIN) 	/* dirname argv[0] */
#if 0
	ADD(OSNAME)	/* uname -s */
	ADD(OSREL)	/* uname -r */
	ADD(PLATFORM)	/* uname -p */
#endif
};

static size_t
expand(char *buf, const char *execname, size_t what, size_t bl)
{
	const char *p, *ep;
	char *bp = buf;
	size_t len;
	char name[32];

	switch (what) {
        case 0:	/* HWCAP XXX: Not yet */
        case 1:	/* ISALIST XXX: Not yet */
            return 0;

        case 2:	/* ORIGIN */
            if (execname == NULL)
                dbg("execname not specified in AUX vector");    // xerr
            if ((ep = strrchr(p = execname, '/')) == NULL)
                dbg("bad execname `%s' in AUX vector", execname);
            break;
#if 0
        case 3:	/* OSNAME */
        case 4:	/* OSREL */
        case 5:	/* PLATFORM */
            len = sizeof(name);
            if (sysctl(mib[what - 3], 2, name, &len, NULL, 0) == -1) {
                xwarn("sysctl");
                return 0;
            }
            ep = (p = name) + len - 1;
            break;
#endif
        default:
            return 0;
	}

	while (p != ep && bl)
		*bp++ = *p++, bl--;

	return bp - buf;
}

#ifndef isalpha
#define isalpha(x) (((x) > 0x40 && (x) < 0x5B) || ((x) > 0x60 && (x) < 0x7B))
#endif



size_t
_rtld_expand_path(char *buf, size_t bufsize, const char *execname,
    const char *bp, const char *ep)
{
	size_t i, ds = bufsize;
	char *dp = buf;
	const char *p;
	int br;

	for (p = bp; p < ep;) {
		if (*p == '$') {
			br = *++p == '{';

			if (br)
				p++;

			for (i = 0; i < sizeof(bltn) / sizeof(bltn[0]); i++) {
				size_t s = bltn[i].namelen;
				const char *es = p + s;

				if ((br && *es != '}') ||
				    (!br && (es != ep &&
					isalpha((unsigned char)*es))))
					continue;

				if (strncmp(bltn[i].name, p, s) == 0) {
					size_t ls = expand(dp, execname, i, ds);
					if (ls >= ds)
						return bufsize;
					ds -= ls;
					dp += ls;
					p = es + br;
					goto done;
				}
			}
			p -= br + 1;

		}
		*dp++ = *p++;
		ds--;
done:;
	}
	*dp = '\0';
	return dp - buf;
}

// searching 

// check if a matching path is already linked to the path-list
static struct _rtld_search_path *
_rtld_find_path(struct _rtld_search_path *path, const char *pathstr, size_t pathlen)
{

	for (; path != NULL; path = path->sp_next) {
		if (pathlen == path->sp_pathlen &&
		    memcmp(path->sp_path, pathstr, pathlen) == 0)
			return path;
	}

	return NULL;
}

struct _rtld_search_path **
_rtld_append_path(struct _rtld_search_path **head_p, struct _rtld_search_path **path_p, const char *execname, const char *bp, const char *ep)
{
    struct _rtld_search_path *path;
	char epath[PATH_MAX];
    char *npath;
	size_t len;

	len = _rtld_expand_path(epath, sizeof(epath), execname, bp, ep);
	if (len == 0)
		return path_p;

	if (_rtld_find_path(*head_p, bp, ep - bp) != NULL)
		return path_p;

    npath = __rtld_state.libc_malloc(len + 1);
    strlcpy(npath, epath, len + 1);

	path = __rtld_state.libc_malloc(sizeof(struct _rtld_search_path));
	path->sp_pathlen = len;
	path->sp_path = npath;
	path->sp_next = (*path_p);
	(*path_p) = path;
	path_p = &path->sp_next;

	dbg(" added path \"%s\"", path->sp_path);

	return path_p;
}

void
_rtld_add_paths(const char *execname, struct _rtld_search_path **path_p, const char *pathstr)
{
    struct _rtld_search_path **head_p = path_p;

    if (pathstr == NULL)
        return;

    if (pathstr[0] == ':') {
        path_p = &(*path_p)->sp_next;
    }

    while (true) {
        const char *bp = pathstr;
        const char *ep = strchr(pathstr, ':');
        if (ep == NULL) {
            ep = &bp[strlen(bp)];
        }

        path_p = _rtld_append_path(head_p, path_p, execname, bp, ep);
    }

    return;
}

struct wasm_module_rt*
_rtld_search_library_path(const char *name, size_t namelen, const char *dir, size_t dirlen, int flags)
{
    struct wasm_module_rt *obj;
    char pathname[PATH_MAX];
    size_t pathnamelen;
    int error;

    obj = NULL;
    pathnamelen = dirlen + 1 + namelen;
    if (pathnamelen >= sizeof(pathname)) {
        return NULL;
    }

    dbg("%s name = '%s' (%d) dir = '%s' (%d)", __func__, name, namelen, dir, dirlen);

    // The elf rtld also stores and check all path that it failed to link.

    //wasm_memory_copy(pathname, dir, dirlen);
    strncpy(pathname, dir, dirlen);
    pathname[dirlen] = '/';
    //wasm_memory_copy(pathname + dirlen + 1, name, namelen);
    strncpy(pathname + dirlen + 1, name, namelen);
    pathname[pathnamelen] = '\0';

    dbg("  Trying \"%s\"", pathname);
    obj = _rtld_load_object(pathname, flags);

    return obj;
}

/*
 *
 */
struct wasm_module_rt *
_rtld_load_library(struct wasm_module_rt *dso_self, const char *name, struct wasm_module_rt *dso_main, int flags)
{
    struct wasm_module_rt *dso;
    struct _rtld_search_path *sp;
    const char *pathname;
    char tmperror[512];
    const char *tmperrorp;
    int namelen;

    if (strchr(name, '/') != NULL) {
        if (name[0] != '/' && __rtld_state.rtld.ld_trust == false) {
            _rtld_error("absolute pathname required for shared object \"%s\"", name);
            return NULL;
        }
        pathname = name;
        goto found;
    }

    dbg(" Searching for \"%s\" (%p)", name, dso_main);

    // temporary storing previous error, so it can later be restored if one dso was sucessfully loaded.
    tmperrorp = __dlerror(NULL);
    if (tmperrorp != NULL) {
        strlcpy(tmperror, tmperrorp, sizeof(tmperror));
        tmperrorp = tmperror;
    }

    namelen = strlen(name);

    for (sp = __rtld_state.rtld.ld_paths; sp != NULL; sp = sp->sp_next) {
        if ((dso = _rtld_search_library_path(name, namelen, sp->sp_path, sp->sp_pathlen, flags)) != NULL)
            goto pathfound;
    }

    if (dso_main != NULL) {
        for (sp = dso_main->rpaths; sp != NULL; sp = sp->sp_next) {
            if ((dso = _rtld_search_library_path(name, namelen, sp->sp_path, sp->sp_pathlen, flags)) != NULL)
                goto pathfound;
        }
    }

    if (dso_self != NULL) {
        for (sp = dso_self->rpaths; sp != NULL; sp = sp->sp_next) {
            if ((dso = _rtld_search_library_path(name, namelen, sp->sp_path, sp->sp_pathlen, flags)) != NULL)
                goto pathfound;
        }
    }

    for (sp = __rtld_state.rtld.ld_default_paths; sp != NULL; sp = sp->sp_next) {
        if ((dso = _rtld_search_library_path(name, namelen, sp->sp_path, sp->sp_pathlen, flags)) != NULL)
            goto pathfound;
    }

    _rtld_error("%s: Shared object \"%s\" not found", dso_main ? dso_main->filepath : NULL, name);
    return NULL;

pathfound:

    if (dso == NULL)
        return NULL;
    
    if (tmperrorp != NULL) {
        _rtld_error("%s", tmperror);
    } else {
        __dlerror(NULL);
    }

    return dso;

found:
    dso = _rtld_load_object(pathname, flags);

    return dso;
}

// reading rtld.exec-hdr custom section

/**
 * Returns a boolean true if a `rtld.exec-hdr` custom section seams to be provided in the given buffer.
 */
bool
_rtld_has_exechdr_in_buf(const uint8_t *buf, uint32_t len, uint32_t *dataoff, uint32_t *datasz)
{
    uint32_t secsz, namesz, lebsz, hdroff, hdrsz;
    const uint8_t *ptr = buf;
    const uint8_t *end = ptr + len;

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

    hdroff = (ptr + namesz) - (uint8_t *)(buf);
    hdrsz = secsz - (namesz + lebsz);

    ptr += namesz;

    if (ptr + 4 < end) {
        hdrsz = *((uint32_t *)(ptr));
    }

    if (dataoff)
        *dataoff = hdroff;

    if (datasz)
        *datasz = hdrsz;

    return true;
}

int
_rtld_read_exechdr(const uint8_t *buf, size_t len, struct wash_exechdr_rt *dsthdr)
{
    struct wasm_exechdr_secinfo *secinfo;
    uint32_t hdrsz, secinfo_cnt;
    void *min_addr;
    void *max_addr;

    hdrsz = *((uint32_t *)(buf)); // possible un-aligned
    if (hdrsz != len || hdrsz > 1024 || dsthdr == NULL) {
        return EINVAL;
    }

    if (hdrsz != dsthdr->hdr_size) {
        return E2BIG;
    }

    min_addr = dsthdr;
    max_addr = dsthdr + hdrsz;
    wasm_memory_copy((void *)dsthdr, buf, hdrsz);

    // string table reloc
    if (dsthdr->runtime_abi != NULL) {
        dsthdr->runtime_abi = (void *)((char *)(dsthdr->runtime_abi) + (uintptr_t)(dsthdr));
    }
    if (dsthdr->secdata != NULL) {
        dsthdr->secdata = (void *)((char *)(dsthdr->secdata) + (uintptr_t)(dsthdr));

        secinfo = dsthdr->secdata;
        secinfo_cnt = dsthdr->section_cnt;
        for (int i = 0; i < secinfo_cnt; i++) {
            if (secinfo->name != NULL)
                secinfo->name += (uintptr_t)(dsthdr);
            secinfo++;
        }
    }

    return (0);
}

struct wasm_exechdr_secinfo *
_rtld_exechdr_find_section(struct wash_exechdr_rt *exehdr, int sectype, const char *secname)
{
    struct wasm_exechdr_secinfo *sec;
    uint32_t namesz, count;

    if (exehdr->section_cnt == 0 || exehdr->secdata == NULL) {
        return NULL;
    }

    if (sectype == WASM_SECTION_CUSTOM) {

        if (secname == NULL) {
            return NULL;
        }

        namesz = strlen(secname);

        sec = exehdr->secdata;
        count = exehdr->section_cnt;
        for (int i = 0; i < count; i++) {
            if (sec->wasm_type == WASM_SECTION_CUSTOM && sec->namesz == namesz && strncmp(sec->name, secname, namesz) == 0) {
                return sec;
            }
            sec++;
        }

    } else {

        sec = exehdr->secdata;
        count = exehdr->section_cnt;
        for (int i = 0; i < count; i++) {
            if (sec->wasm_type == sectype) {
                return sec;
            }
            sec++;
        }
    }

    return NULL;
}

struct rtld_segment *
_rtld_find_data_segment(struct wasm_module_rt *obj, const char *name)
{
    struct rtld_segment *data;
    uint32_t namesz;
    uint32_t count;

    if (obj == NULL || name == NULL)
        return NULL;

    count = obj->data_segments_count;
    if (count == 0)
        return NULL;

    namesz = strlen(name);
    data = obj->data_segments;
    for (int i = 0; i < count; i++) {
        if (data->namesz == namesz && strncmp(name, data->name, namesz) == 0) {
            return data;
        }
        data++;
    }

    return NULL;
}

// loading

 struct builtin_name {
    uint8_t namesz;
    const char *name;
};

static const struct builtin_name _rtld_common_names[] = {
    {
        .namesz = 7,
        .name = ".rodata"
    },
    {
        .namesz = 5,
        .name = ".data"
    },
    {
        .namesz = 4,
        .name = ".bss"
    },
    {
        .namesz = 7,
        .name = ".dynstr"
    },
    {
        .namesz = 7,
        .name = ".dynsym"
    },
    {
        .namesz = 11,
        .name = ".init_array"
    },
    {
        .namesz = 11,
        .name = ".fnit_array"
    },
};

const char *
_rtld_find_common_segment_name(const char *name, uint32_t namesz)
{
    if (name == NULL || namesz == 0) {
        return NULL;
    }

    const struct builtin_name *p;
    uint32_t cnt = __arraycount(_rtld_common_names);
    for (int i = 0; i < cnt; i++) {
        p = &_rtld_common_names[i];
        if (namesz == p->namesz && strncmp(name, p->name, namesz) == 0) {
            return p->name;
        }
    }

    return NULL;
}

// Relocation of code & data-segments

#define NBDL_SUBSEC_MODULES 0x01
#define NBDL_SUBSEC_RLOCEXT 0x07
#define NBDL_SUBSEC_RLOCINT 0x08

struct rtld_dylink0_subsection {
    uint8_t hdr_size;   // size of the chunk of data until positioned at count.
    uint8_t type;
    void *subsec_p;
    void *subsec_data_p;
    uint32_t size;
};

union i32_value {
    uint32_t value;
    unsigned char bytes[4];
};

struct execbuf_mapping {
    struct section_mapping *next;
    struct wasm_exechdr_secinfo *sec;
    uintptr_t addr;     // in-memory address (if temporary mapped in memory)
    uint32_t dst_offset;
};

int
rtld_reloc_memory_limits(struct wasm_module_rt *obj, struct execbuf_mapping *imports, struct execbuf_mapping *memory)
{
    return (0);
}

int
_rtld_dylink0_find_subsection(uint8_t *dylink0_p, uint8_t *dylink0_end, struct rtld_dylink0_subsection *subsec, uint8_t type)
{
    uint32_t lebsz, secsz, namesz, subsecsz, cnt;
    uint8_t subsec_type;
    uint8_t *ptr, *subsec_start;

    if (subsec->hdr_size == 0) {

        ptr = dylink0_p;
        if (*(ptr) != WASM_SECTION_CUSTOM) {
            dbg("%s returning since its not a custom section @+%lx", __func__, (uintptr_t)ptr);
            return -1;
        }

        ptr++;
        secsz = decodeULEB128(ptr, &lebsz, dylink0_end, NULL);
        ptr += lebsz;

        namesz = decodeULEB128(ptr, &lebsz, dylink0_end, NULL);
        ptr += lebsz + namesz;

        subsec->hdr_size = (ptr - dylink0_p);
    } else {
        ptr = dylink0_p + subsec->hdr_size;
    }

    cnt = decodeULEB128(ptr, &lebsz, dylink0_end, NULL);
    ptr += lebsz;

    for (int i = 0; i < cnt; i++) {
        subsec_start = ptr;
        subsec_type = *(ptr);
        ptr++;

        subsecsz = decodeULEB128(ptr, &lebsz, dylink0_end, NULL);
        ptr += lebsz;

        dbg("%s ptr = %p type = %d size = %d", __func__, subsec_start, subsec_type, subsecsz);

        if (subsec_type == type) {
            subsec->subsec_p = subsec_start;
            subsec->subsec_data_p = ptr;
            subsec->size = subsecsz;
            return (0);
        }

        ptr += subsecsz;
    }


    return -1;
}


int
_rtld_do_internal_data_reloc(struct wasm_module_rt *obj, char *relocbuf, uint32_t relocbufsz, const char *dylink0_start, const char *dylink0_end)
{
    struct rtld_dylink0_subsection *section;
    struct wasm_exechdr_secinfo *dylink0sec;
    struct rtld_segment *data_segments;
    struct rtld_segment *elem_segments;
    int error;
    uint32_t elem_count, data_count;
    uint32_t count, lebsz;
    uint8_t *ptr, *end, *ptr_start, *rloc, *sec_start;
    const char *errstr;
    struct rtld_dylink0_subsection subsec;
    union i32_value i32;

    // find interal reloc section
    wasm_memory_fill(&subsec, 0, sizeof(struct rtld_dylink0_subsection));

    error = _rtld_dylink0_find_subsection((uint8_t *)dylink0_start, (uint8_t *)dylink0_end, &subsec, NBDL_SUBSEC_RLOCINT);
    if (error != 0) {
        dbg("%s returning EINVAL due to reloc-internal subsection not found.", __func__);
        return EINVAL;
    }
    
    ptr_start = subsec.subsec_data_p;
    end = ptr_start + subsec.size;
    ptr = ptr_start;
    errstr = NULL;
    
    // TODO: this loading must adopt to reading back chunks..

    elem_segments = obj->elem_segments;
    elem_count = obj->elem_segments_count;

    data_segments = obj->data_segments;
    data_count = obj->data_segments_count;

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
            dst_base = data_segments[dst_idx].addr;
            dst_max = (dst_base + data_segments[dst_idx].size) - 4;
            //printf("%s dst_base = %d of rloctype = %d\n", __func__, dst_base, rloctype);

            src_type = *(ptr);
            ptr++;
            src_idx = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            if (src_type == 1) { // data-segment
                if (src_idx >= data_count) {
                    dbg("%s src_idx %d to large for data count %d\n", __func__, src_idx, data_count);
                    error = EINVAL;
                    goto errout;
                }
                src_base = data_segments[src_idx].addr;
            } else if (src_type == 2) { // elem-segment
                if (src_idx >= elem_count) {
                    dbg("%s src_idx %d to large for elem count %d\n", __func__, src_idx, elem_count);
                    error = EINVAL;
                    goto errout;
                }
                src_base = elem_segments[src_idx].addr;
            } else {
                dbg("%s INVALID_SRC_TYPE = %d\n", __func__, src_type);
                error = EINVAL;
                goto errout;
            }

            rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            //dbg("%s src_base = %d of src_type = %d rloc_count = %d\n", __func__, src_base, src_type, rloc_count);

            for (int x = 0; x < rloc_count; x++) {
                dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                dst_addr = dst_base + dst_off;                
                if (dst_addr < dst_base || dst_addr > dst_max) {
                    dbg("%s ERROR i32_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d) segment = %s\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base, data_segments[dst_idx].name);
                }

                i32.value = src_base + src_off;
                rloc = (uint8_t *)dst_addr;
                rloc[0] = i32.bytes[0];
                rloc[1] = i32.bytes[1];
                rloc[2] = i32.bytes[2];
                rloc[3] = i32.bytes[3];
            }

        }

        ptr = sec_start + chunksz;
    }

    dbg("%s did all relocs\n", __func__);

    return (0);

errout: 

    dbg("%s ERROR %d failed to complete relocs\n", __func__, error);

    return error;
}

/*
 * Internal reloc are those that is linked between symbols & addresses within the object itself.
 */
int
rtld_do_internal_reloc_on_module(struct wasm_module_rt *obj, char *relocbuf, uint32_t relocbufsz, struct execbuf_mapping *dylink0, struct execbuf_mapping *bytecode)
{
    struct rtld_dylink0_subsection *section;
    struct wasm_exechdr_secinfo *dylink0sec;
    struct rtld_segment *data_segments;
    struct rtld_segment *elem_segments;
    int error;
    uint32_t elem_count, data_count;
    uint32_t count, lebsz;
    uint8_t *ptr, *end, *ptr_start, *rloc, *sec_start;
    uint8_t *codesec_p, *dylink0_p, *coderloc_p, *coderloc_end;
    const char *errstr;
    struct rtld_dylink0_subsection subsec;
    union i32_value i32;

    // find interal reloc section
    wasm_memory_fill(&subsec, 0, sizeof(struct rtld_dylink0_subsection));
    dylink0_p = (uint8_t *)dylink0->addr;
    dylink0sec = dylink0->sec;
    error = _rtld_dylink0_find_subsection(dylink0_p, dylink0_p + dylink0sec->sec_size, &subsec, NBDL_SUBSEC_RLOCINT);
    if (error != 0) {
        dbg("%s returning EINVAL due to reloc-internal subsection not found.", __func__);
        return EINVAL;
    }

    if (bytecode) {
        codesec_p = (uint8_t *)bytecode->addr;
        coderloc_p = codesec_p + bytecode->sec->hdrsz;
        coderloc_end = coderloc_p + (bytecode->sec->sec_size - bytecode->sec->hdrsz);
    } else {
        codesec_p = NULL;
        coderloc_p = NULL;
        coderloc_end = NULL;
    }
    
    ptr_start = subsec.subsec_data_p;
    end = ptr_start + subsec.size;
    ptr = ptr_start;
    errstr = NULL;
    
    // TODO: this loading must adopt to reading back chunks..

    elem_segments = obj->elem_segments;
    elem_count = obj->elem_segments_count;

    data_segments = obj->data_segments;
    data_count = obj->data_segments_count;

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
#if 0
        if (rloctype == R_WASM_MEMORY_ADDR_I32 || rloctype == R_WASM_TABLE_INDEX_I32) {
            dst_idx = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            if (dst_idx >= data_count) {
                //printf("%s dst_idx %d to large\n", __func__, dst_idx);
            }
            dst_base = data_segments[dst_idx].addr;
            dst_max = (dst_base + data_segments[dst_idx].size) - 4;
            //printf("%s dst_base = %d of rloctype = %d\n", __func__, dst_base, rloctype);

            src_type = *(ptr);
            ptr++;
            src_idx = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            if (src_type == 1) { // data-segment
                if (src_idx >= data_count) {
                    dbg("%s src_idx %d to large for data count %d\n", __func__, src_idx, data_count);
                    error = EINVAL;
                    goto errout;
                }
                src_base = data_segments[src_idx].addr;
            } else if (src_type == 2) { // elem-segment
                if (src_idx >= elem_count) {
                    dbg("%s src_idx %d to large for elem count %d\n", __func__, src_idx, elem_count);
                    error = EINVAL;
                    goto errout;
                }
                src_base = elem_segments[src_idx].addr;
            } else {
                dbg("%s INVALID_SRC_TYPE = %d\n", __func__, src_type);
                error = EINVAL;
                goto errout;
            }

            rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            //dbg("%s src_base = %d of src_type = %d rloc_count = %d\n", __func__, src_base, src_type, rloc_count);

            for (int x = 0; x < rloc_count; x++) {
                dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                dst_addr = dst_base + dst_off;                
                if (dst_addr < dst_base || dst_addr > dst_max) {
                    dbg("%s ERROR i32_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d) segment = %s\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base, data_segments[dst_idx].name);
                }

                i32.value = src_base + src_off;
                rloc = (uint8_t *)dst_addr;
                rloc[0] = i32.bytes[0];
                rloc[1] = i32.bytes[1];
                rloc[2] = i32.bytes[2];
                rloc[3] = i32.bytes[3];
            }

        } else {
#endif
        if (rloctype != R_WASM_MEMORY_ADDR_I32 && rloctype != R_WASM_TABLE_INDEX_I32) {
            src_type = *(ptr);
            ptr++;
            src_idx = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            if (coderloc_p == NULL) {
                dbg("%s ERROR could not find code-section for code-reloc\n", __func__);
                error = ENOENT;
                goto errout;
            }

            dst_base = (uint32_t)coderloc_p;
            dst_max = (uint32_t)(coderloc_end - 5);

            if (src_type == 1) { // data-segment
                if (src_idx >= data_count) {
                    dbg("%s ERROR src_idx %d to large for data count %d\n", __func__, src_idx, data_count);
                    error = EINVAL;
                    goto errout;
                }
                src_base = data_segments[src_idx].addr;
                //dbg("%s src_base = %d of type = %d\n", __func__, src_base, src_type);
            } else if (src_type == 2) { // elem-segment
                if (src_idx >= elem_count) {
                    dbg("%s ERROR src_idx %d to large for elem count %d\n", __func__, src_idx, elem_count);
                    error = EINVAL;
                    goto errout;
                }
                src_base = elem_segments[src_idx].addr;
                //dbg("%s src_base = %d of type = %d\n", __func__, src_base, src_type);
            } else {
                //dbg("%s INVALID_SRC_TYPE = %d\n", __func__, src_type);
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
                        dbg("%s ERROR leb_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d)\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base);
                    }
                    rloc = (uint8_t *)dst_addr;
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
                        dbg("%s ERROR leb_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d)\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base);
                    }
                    rloc = (uint8_t *)dst_addr;
                    encodeSLEB128(src_base + src_off, rloc, 5);
                }
            }
        }

        ptr = sec_start + chunksz;
    }

    dbg("%s did all relocs\n", __func__);

    return (0);

errout: 

    dbg("%s ERROR %d failed to complete relocs\n", __func__, error);

    return error;

}

/**
 * External reloc relies on .dynsym data-section and __dlsym_internal() for lookup of symbols. This does not fail
 * upon circular dependencies, since both __indirect_table location and memory have been copied of all objects
 * in the dependency chain at this point.
 */
int
rtld_do_extern_reloc_on_module(struct wasm_module_rt *obj, char *relocbuf, uint32_t relocbufsz, struct execbuf_mapping *dylink0, struct execbuf_mapping *bytecode)
{
    // extern reloc is similar to internal, only that these are weakly linked trough a name which are checked against other modules.
    struct wasm_exechdr_secinfo *dylink0sec;
    struct rtld_segment *data_segments;
    struct _rtld_needed_entry *needed_head, *needed;
    struct wasm_module_rt *nobj, *objglob;
    struct dlsym_rt *sym;
    char *name;
    uint32_t namesz;
    int error;
    uint32_t data_count;
    uint32_t count, lebsz;
    uint32_t execfd;
    uint32_t rloc_count;
    uint8_t *ptr, *end, *ptr_start, *chunk_start;
    uint8_t *codesec_p, *dylink0_p, *coderloc_p, *coderloc_end;
    const char *errstr;
    struct rtld_dylink0_subsection subsec;
    union i32_value i32;

    // find section
    // find section
    wasm_memory_fill(&subsec, 0, sizeof(struct rtld_dylink0_subsection));
    dylink0_p = (uint8_t *)dylink0->addr;
    dylink0sec = dylink0->sec;
    error = _rtld_dylink0_find_subsection(dylink0_p, dylink0_p + dylink0sec->sec_size, &subsec, NBDL_SUBSEC_RLOCEXT);
    if (error != 0) {
        dbg("%s returning EINVAL due to reloc-extern subsection not found.", __func__);
        return EINVAL;
    }

    dbg("%s external reloc on object = %s addr = %p\n", __func__, obj->dso_name, obj);

    if (bytecode) {
        codesec_p = (uint8_t *)bytecode->addr;
        coderloc_p = codesec_p + bytecode->sec->hdrsz;
        coderloc_end = coderloc_p + (bytecode->sec->sec_size - bytecode->sec->hdrsz);
    } else {
        codesec_p = NULL;
        coderloc_p = NULL;
        coderloc_end = NULL;
    }
    
    ptr_start = subsec.subsec_data_p;
    end = ptr_start + subsec.size;
    ptr = ptr_start;
    
    errstr = NULL;

    data_segments = obj->data_segments;
    data_count = obj->data_segments_count;

    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    dbg("%s reloc by extern symbol count %d object = %s\n", __func__, count, obj->dso_name);


    // 
    needed_head = obj->needed;

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
            dbg("non-supported symtype %d in module %s\n", symtype, obj->dso_name);
            symtype = 255;
        }

        sym = NULL;

        for (needed = needed_head; needed != NULL; needed = needed->next) {
            
            nobj = needed->obj;
            if (nobj == NULL) {
                dbg("%s needed object is NULL for needed %p\n", __func__, needed);
                continue; // should abort here!
            }

            if (nobj->dlsym_start == NULL) {
                dbg("%s module = %s (%p) missing .dynsym data..\n", __func__, nobj->dso_name, nobj);
                continue;
            }
            
            sym = __dlsym_internal((struct dlsym_rt *)nobj->dlsym_start, (struct dlsym_rt *)nobj->dlsym_end, namesz, name, symtype);
            if (sym == NULL) {
                continue;
            }

            src_base = sym->addr;
            dbg_loading("%s found symbol '%.*s' on needed object '%s' at = %p\n", __func__, namesz, name, nobj->dso_name, (void *)src_base);
            
            break;
        }

        // IF not found check from objhead for GLOBAL
        if (sym == NULL) {
            bool is_in_needed;
            for (objglob = __rtld_state.rtld.objlist; objglob != NULL; objglob = objglob->next) {

                if (objglob == obj || objglob->dlsym_start == NULL) {
                    continue;
                }

                // TODO: this is not the most effective way to check this.. (should compile this before the loop)
                is_in_needed = false;
                for (needed = needed_head; needed != NULL; needed = needed->next) {
                    if (needed->obj == objglob) {
                        is_in_needed = true;
                        break;
                    }
                }

                if (is_in_needed) {
                    continue;
                }
                
                sym = __dlsym_internal((struct dlsym_rt *)objglob->dlsym_start, (struct dlsym_rt *)objglob->dlsym_end, namesz, name, symtype);
                if (sym == NULL) {
                    continue;
                }

                src_base = sym->addr;
                dbg_loading("%s found symbol '%.*s' on global object '%s' at = %p\n", __func__, namesz, name, objglob->dso_name, (void *)src_base);
                break;
            }
        }

        if (sym == NULL) {
            dbg("%s symbol '%.*s' not found (namesz = %d chunksz = %d)\n", __func__, namesz, name, namesz, chunksz);
            ptr = chunk_start + chunksz;
            continue;
        }

        // uleb relocs
        rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (rloc_count > 0) {

            dst_base = coderloc_p;
            dst_max = coderloc_end - 5;

            for (int x = 0; x < rloc_count; x++) {
                dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                dst_addr = dst_base + dst_off;
                if (dst_addr <= dst_base || dst_addr > dst_max) {
                    dbg("%s ERROR leb_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d)\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base);
                }
                encodeULEB128(src_base + src_off, dst_addr, 5);
            }
        }

        // sleb relocs
        rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (rloc_count > 0) {

            dst_base = coderloc_p;
            dst_max = coderloc_end - 5;

            for (int x = 0; x < rloc_count; x++) {
                dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                dst_addr = dst_base + dst_off;
                if (dst_addr <= dst_base || dst_addr > dst_max) {
                    dbg("%s ERROR leb_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d)\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base);
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
                    dbg("%s dst_idx %d to large (in %d of %d, ptr = %p ptr_start %p)\n", __func__, dst_idx, x, rloc_count, ptr, ptr_start);
                }
                dst_base = (uint8_t *)data_segments[dst_idx].addr;
                dst_max = dst_base + (data_segments[dst_idx].size - 4);
                dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                dst_addr = dst_base + dst_off;
                if (dst_addr < dst_base || dst_addr > dst_max) {
                    dbg("%s ERROR i32_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d) segment = %s\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base, data_segments[dst_idx].name);
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

    dbg("%s did all external relocs\n", __func__);

    return (0);

errout: 

    dbg("%s failed to complete relocs\n", __func__);

    return error;
}

#define WASM_IMPORT_KIND_FUNC 0x00
#define WASM_IMPORT_KIND_TABLE 0x01
#define WASM_IMPORT_KIND_MEMORY 0x02
#define WASM_IMPORT_KIND_GLOBAL 0x03
#define WASM_IMPORT_KIND_TAG 0x04

/**
 * WebAssembly modules requires memory descriptors to explicity declare minimum and maximum of the memory container.
 * This values must match the values on the WebAssembly.Memory linked by the importObject on that property, to walkaround
 * this limititation the linker writes out these leb128 with padding, so that rtld can change it before compiling the module.
 */
int
rtld_setup_memory_descriptors(struct wasm_module_rt *obj,  char *relocbuf, uint32_t relocbufsz, struct execbuf_mapping *imports_map, struct execbuf_mapping *memory_map)
{
    struct wasm_module_rt *objmain;
    struct rtld_memory_descriptor *memdesc;
    const char *errstr;
    uint8_t *ptr, *ptr_start, *end, *symbol_start;
    uint32_t lebsz, secsz, index, count;
    char *module_name;
    uint32_t module_namesz;
    char *name;
    uint32_t namesz;
    uint32_t kind;
    int error;

    if (imports_map == NULL) {
        dbg("%s returning ENOENT due to imports section not defined..", __func__);
        return ENOENT;
    }

    ptr = (uint8_t *)imports_map->addr;
    end = ptr + imports_map->sec->sec_size;
    ptr_start = ptr;

    if (*(ptr) != WASM_SECTION_IMPORT) {
        dbg("%s not a import section mapped at addr %p.", __func__, ptr);
        return ENOENT;
    }

    ptr++;
    secsz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;
    
    if (secsz != (imports_map->sec->sec_size - (ptr - ptr_start))) {
        dbg("%s import section size %d inconstistent with exexhdr section's marked size %d", __func__, secsz, imports_map->sec->sec_size);
        return EINVAL;
    }

    errstr = NULL;
    index = 0;
    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    for (int i = 0; i < count; i++) {
        symbol_start = ptr;
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
            uint8_t *min_leb_p;
            uint8_t *max_leb_p;
            uint8_t *limit_p;
            uint32_t min;
            uint32_t max;
            uint32_t min_lebsz;
            uint32_t max_lebsz;
            bool shared;
            uint8_t limit;

            limit_p = ptr;
            min_leb_p = NULL;
            max_leb_p = NULL;
            min_lebsz = 0;
            max_lebsz = 0;

            limit = *(ptr);
            ptr++;
            
            if (limit == 0x01) {
                min_leb_p = ptr;
                min = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                min_lebsz = lebsz;

                max_leb_p = ptr;
                max = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                max_lebsz = lebsz;
                shared = false;
            } else if (limit == 0x00) {
                min_leb_p = ptr;
                min = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                min_lebsz = lebsz;
                shared = false;
            } else if (limit == 0x02) {
                min_leb_p = ptr;
                min = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                min_lebsz = lebsz;
                shared = true;
            } else if (limit == 0x03) {
                min_leb_p = ptr;
                min = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                min_lebsz = lebsz;

                max_leb_p = ptr;
                max = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                max_lebsz = lebsz;
                shared = true;
            }

            objmain = __rtld_state.rtld.objmain;
            
            if (objmain != NULL && objmain->memdesc != NULL) {
                memdesc = objmain->memdesc;
                encodeULEB128(memdesc->initial, min_leb_p, min_lebsz);
                encodeULEB128(memdesc->maximum, max_leb_p, max_lebsz);
                dbg("%s did setup memdesc min-leb %p value = %d max-leb %p value %d", __func__, min_leb_p, memdesc->initial, max_leb_p, memdesc->maximum);
                return (0);
            } else {
                error = EINVAL;
                goto error_out;
            }

            
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

    error = ENOENT;

error_out:

    dbg("%s failed to complete setup of memory descriptor(s) with error = %d\n", __func__, error);

    return error;
}

// loading object & reading info

#define DYLINK_SUBSEC_HEADING 1

int
_rtld_read_dylink0_early(struct wasm_module_rt *obj, struct wash_exechdr_rt *exechdr, int fd, const char *buf, uint32_t bufsz, uint32_t file_offset)
{
    struct wasm_module_rt *depobj;
    struct wasm_exechdr_secinfo *data_sec;
    struct rtld_segment *seg, *elem_segments, *data_segments;
    struct _rtld_needed_entry *needed_tail, *needed;
    char tmperr[128];
    char tmpname[NAME_MAX + 1];
    char tmpvers[VERS_MAX + 1];
    const char *errstr;
    uint32_t lebsz, secsz, namesz, verssz;
    uint32_t subcnt, subsz, cnt, max_count, vers_count, vers_type;
    uint32_t min_data_off, max_data_off, data_off_start;
    uint8_t *ptr, *end, *ptr_start;
    char *tmpbuf;
    char *namep, *versp;
    int error;

    tmpbuf = NULL;
    ptr = (uint8_t *)buf;
    end = ptr + bufsz;
    ptr_start = ptr;
    errstr = NULL;
    data_segments = NULL;
    elem_segments = NULL;

    if (*(ptr) != WASM_SECTION_CUSTOM) {
        dbg("%s not a rtld.dylink.0 section..\n", __func__);
        return ENOENT;
    }

    ptr++;
    secsz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;
    if (namesz != 13 || strncmp((const char *)ptr, "rtld.dylink.0", namesz) != 0) {
        dbg("%s not a rtld.dylink.0 section..\n", __func__);
        return ENOENT;
    }
    ptr += namesz;

    subcnt = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    if (*(ptr) != DYLINK_SUBSEC_HEADING) {
        dbg("%s sub-section heading not first section.\n", __func__);
        return ENOENT;
    }

    ptr++;

    subsz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;
    if (ptr + subsz <= end) {
        end = (ptr + subsz);
    } else {
        // if the binary has a large heading alloc a temporary chunk.
        tmpbuf = __rtld_state.libc_malloc(namesz + 1);
        if (tmpbuf == NULL) {
            return ENOMEM;
        }
        file_offset = file_offset + (ptr - ptr_start);
        error = __rtld_state.__sys_lseek(fd, file_offset, SEEK_SET);
        error = __rtld_state.__sys_read(fd, tmpbuf, secsz);
        ptr = (uint8_t *)tmpbuf;
        end = ptr + subsz;
        ptr_start = ptr;
    }

    // uleb namesz + name bytes
    // uleb verssz + vers bytes

    namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;
    if (namesz > NAME_MAX) {
        dbg("%s obj->name is indicated to be to long %d\n", __func__, namesz);
        error = ENAMETOOLONG;
        goto error_out;
    }
    if (namesz != 0) {
        namep = __rtld_state.libc_malloc(namesz + 1);
        if (namep == NULL) {
            error = ENOMEM;
            goto error_out;
        }
        obj->dso_name = namep;
        obj->dso_namesz = namesz;
        strlcpy(namep, (const char *)ptr, namesz + 1);
        ptr += namesz;
    } else {
        namep = NULL;
    }

    verssz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;
    if (verssz > VERS_MAX) {
        dbg("%s obj->vers is indicated to be to long %d\n", __func__, namesz);
        error = ENAMETOOLONG;
        goto error_out;
    }
    if (verssz != 0) {
        versp = __rtld_state.libc_malloc(verssz + 1);
        if (versp == NULL) {
            error = ENOMEM;
            goto error_out;
        }
        obj->dso_vers = versp;
        obj->dso_verssz = verssz;
        strlcpy(versp, (const char *)ptr, verssz + 1);
        ptr += verssz;
    } else {
        versp = NULL;
    }

    // uleb dependencies count
    // - uleb namesz + name bytes
    // - uleb verssz + vers bytes 
    // unlike ELF this are not placed in the .dynstr memory segment yet, its within the dylink.0 section which is
    // not loader or keept at runtime (only temporary during linking) so we copy these string at the end of the
    // memory allocated for the need struct.
    cnt = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;
    needed_tail = obj->needed;

    for (int i = 0; i < cnt; i++) {

        const char *src_n, *src_v;
        char *strdst;
        uint32_t memsz = sizeof(struct _rtld_needed_entry); 
        uint8_t type = *(ptr);
        ptr++;


        namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (namesz > NAME_MAX) {
            dbg("%s dep->name is indicated to be to long %d at index %d\n", __func__, namesz, i);
            error = ENAMETOOLONG;
            goto error_out;
        } else if (namesz != 0) {
            memsz += (namesz + 1);
            src_n = (const char *)ptr;
            ptr += namesz;
        } else {
            dbg("%s dep->name is NULL at index %d\n", __func__, i);
        }
        

        vers_count = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (vers_count != 0) {
            vers_type = *(ptr);
            ptr++;
            if (vers_type == 1) {
                verssz = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                if (verssz > VERS_MAX) {
                    dbg("%s dep->vers is indicated to be to long %d at index %d\n", __func__, namesz, i);
                    error = ENAMETOOLONG;
                    goto error_out;
                } else if (verssz != 0) {
                    memsz += (verssz + 1);
                    src_v = (const char *)ptr;
                    ptr += verssz;
                }
            } else {
                dbg("%s dep->vers (type %d) is not used at dep index %d\n", __func__, vers_type, i);
            }
        } else {
            src_v = NULL;
            verssz = 0;
        }

        needed = __rtld_state.libc_malloc(memsz);
        if (needed == NULL) {
            error = ENOMEM;
            goto error_out;
        }

        // zero fill
        wasm_memory_fill(needed, 0, sizeof(struct _rtld_needed_entry));

        needed->namesz = namesz;
        needed->verssz = verssz;

        strdst = (char *)needed;
        strdst = strdst + sizeof(struct _rtld_needed_entry);
        strlcpy(strdst, src_n, namesz + 1);
        needed->name = strdst;

        if (src_v) {
            strdst = strdst + namesz + 1;
            strlcpy(strdst, src_v, verssz + 1);
            needed->vers = strdst;
        }

        if (needed_tail == NULL) {
            obj->needed = needed;
            needed_tail = needed;
        } else {
            needed_tail->next = needed;
            needed_tail = needed;
        }
    }

    // element segments
    cnt = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    if (cnt > 0) {

        elem_segments = __rtld_state.libc_malloc(sizeof(struct rtld_segment) * cnt);
        if (elem_segments == NULL) {
            error = ENOMEM;
            goto error_out;
        }

        // zero fill
        wasm_memory_fill(elem_segments, 0, sizeof(struct rtld_segment) * cnt);

        // put references now already, so that any name allocation can be freed using object freeing method.
        obj->elem_segments = elem_segments;
        obj->elem_segments_count = cnt;

        seg = elem_segments;
        for (int i = 0; i < cnt; i++) {
            uint8_t type, vers_type;
            uint32_t segidx, seg_align, seg_size;
            type = *(ptr);
            ptr++;
            namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            namep = NULL;
            if (namesz > NAME_MAX) {
                dbg("%s obj->elem_segment[%d]->name is indicated to be to long %d\n", __func__, i, namesz);
                error = ENAMETOOLONG;
                goto error_out;
            } else if (namesz != 0) {
                namep = __rtld_state.libc_malloc(namesz + 1);   // TODO: what if we fail here?
                if (namep != NULL)
                    strlcpy(namep, (const char *)ptr, namesz + 1);
                ptr += namesz;
            }
            segidx = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            seg_align = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
            
            seg_size = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            // TODO: might insert a field to indicate size of element segments data.

            seg->type = WASM_TYPE_FUNCREF;
            seg->namesz = namesz;
            seg->name = namep;
            seg->size = seg_size;
            seg->align = seg_align;

            dbg("%s element-segment @%p type = %d name = %s (namesz = %d) size = %d (data-size: %d) align = %d\n", __func__, seg, type, seg->name, namesz, (uint32_t)seg->size, 0, seg->align);

            seg++;
        }
    }

    // data segments
    cnt = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    if (cnt > 0) {

        data_segments = __rtld_state.libc_malloc(sizeof(struct rtld_segment) * cnt);
        if (data_segments == NULL) {
            error = ENOMEM;
            goto error_out;
        }

        // zero fill
        wasm_memory_fill(data_segments, 0, sizeof(struct rtld_segment) * cnt);

        obj->data_segments = data_segments;
        obj->data_segments_count = cnt;

        data_sec = _rtld_exechdr_find_section(exechdr, WASM_SECTION_DATA, NULL);
        // data_sec might be null if binary only uses .bss

        if (data_sec) {
            data_off_start = data_sec->file_offset + data_sec->hdrsz;
            min_data_off = 1;                           // TODO: + lebsz for count
            max_data_off = data_sec->sec_size;          // TODO: - lebsz for count
        } else {
            data_off_start = 0;
            min_data_off = 1;   // makes any use of dataoff trigger the range guard.
            max_data_off = 0;
        }

        seg = data_segments;
        for (int i = 0; i < cnt; i++) {

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

            namep = NULL;

            if (dataoff >= min_data_off && dataoff < max_data_off) {
                dataoff += data_off_start;
            } else if (dataoff != 0) {
                dbg("%s data-offset %d for data segment is out of range (min: %d max: %d) %d\n", __func__, dataoff, min_data_off, max_data_off);
                error = EINVAL;
                goto error_out;
            }
            
            if (namesz > NAME_MAX) {
                dbg("%s obj->data_segment[%d]->name is indicated to be to long %d\n", __func__, i, namesz);
                error = ENAMETOOLONG;
                goto error_out;
            } else if (namesz != 0) {

                namep = (char *)_rtld_find_common_segment_name((const char *)ptr, namesz);
                if (namep != NULL) {
                    flags |= _RTLD_SEGMENT_COMMON_NAME;
                } else {
                    namep = __rtld_state.libc_malloc(namesz + 1);
                    if (namep)
                        strlcpy(namep, (const char *)ptr, namesz + 1);
                }

                ptr += namesz;
            }

            seg->flags = flags;
            seg->align = max_align;
            seg->namesz = namesz;
            seg->name = namep;
            seg->size = size;
            seg->src_offset = dataoff;

            dbg("%s index = %d name %s size = %d align = %d data-offset = %d flags = %d\n", __func__, i, seg->name, (uint32_t)seg->size, seg->align, seg->src_offset, seg->flags);
            seg++;
        }
    }

    if (ptr >= end) {
        if (tmpbuf)
            __rtld_state.libc_free(tmpbuf);
        return (0);
    }

    cnt = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    // uleb field count
    // - u8 type
    // - uleb field size
    // - bytes of field size
    // ... repeat of field count ....

    if (tmpbuf)
        __rtld_state.libc_free(tmpbuf);

    return (0);

error_out:

    if (tmpbuf)
        __rtld_state.libc_free(tmpbuf);
    
    // elem_segments, data_segments if allocated are freed using _rtld_obj_free()

    return error;
}

#if 0
/**
 * Reads the outline of the data-segments by a sequence of read operations on the shared object file.
 */
int
_rtld_read_data_segments_info(struct wasm_module_rt *obj, struct wash_exechdr_rt *exehdr, int fd, char *buf, uint32_t bufsz)
{
    struct wasm_exechdr_secinfo *section;
    struct rtld_segment *data_segments, *segment;
    uint8_t kind;
    uint32_t ret, count, lebsz, secsz;
    uint32_t file_offset;
    uint8_t *ptr, *end, *ptr_start, *file_start;
    const char *errstr;
    int error;

    section = _rtld_exechdr_find_section(exehdr, WASM_SECTION_DATA, NULL);
    if (section == NULL || section->wasm_type != WASM_SECTION_DATA) {
        return ENODATA;
    }

    ptr = (uint8_t *)buf;
    end = ptr + bufsz;
    ptr_start = ptr;
    errstr = NULL;

    file_offset = section->file_offset;
    ret = __rtld_state.__sys_lseek(fd, file_offset, SEEK_SET);
    ret = __rtld_state.__sys_read(fd, buf, bufsz);

    if (*(ptr) != WASM_SECTION_DATA) {
        dbg("%s not a data section at offset %d", __func__, file_offset);
        return EINVAL;
    }

    ptr++;
    secsz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    if (secsz != (section->sec_size - (ptr - ptr_start))) {
        dbg("%s in section size %d does not match stored size %d -(type+lebsz)", __func__, secsz, section->sec_size);
        return EINVAL;
    }
    
    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    data_segments = __rtld_state.libc_malloc(sizeof(struct rtld_segment) * count);
    if (data_segments == NULL)
        return ENOMEM;

    wasm_memory_fill(data_segments, 0, sizeof(struct rtld_segment) * count);

    segment = data_segments;
    for (int i = 0; i < count; i++) {
        uint32_t size;
        uint32_t kind = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (kind != 0x01) {
            dbg("%s found non passive data-segment near %lu\n", __func__, ptr - ptr_start);
            error = EINVAL;
        }
        size = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        segment->type = kind;
        segment->size = size;
        segment->src_offset = file_offset + (uint32_t)(ptr - ptr_start);   // we might be able to use addr before its determined where to place it.

        dbg("%s data-segment type = %d size = %d src_offset = %d\n", __func__, segment->type, (uint32_t)segment->size, segment->src_offset);
        segment++;
        ptr += size;

        // if there is not atleast 20 bytes to read read at the start of the next segment.
        if (ptr > (end - 20) && i != count - 1) {
            file_offset = file_offset + (ptr - ptr_start);
            ret = __rtld_state.__sys_lseek(fd, file_offset, SEEK_SET);
            ret = __rtld_state.__sys_read(fd, buf, bufsz);
            ptr = ptr_start;
        }
    }

    obj->data_segments_count = count;
    obj->data_segments = data_segments;

    // uleb kind (should be u8 I think?)
    // uleb size
    // bytes[size]
    // 
    // only kind == 0x01 is easy to support (we could also put data-segment into a custom section?)

    return (0);

error_out:

    if (data_segments)
        __rtld_state.libc_free(data_segments);

    return (error);
}
#endif

void
_rtld_obj_free(struct wasm_module_rt*obj)
{
    if ((obj->flags & _RTLD_OBJ_MALLOC) == 0) {
        return;
    }

    if (obj->fd != -1) {
        __rtld_state.__sys_close(obj->fd);
        obj->fd = -1;
    }

    if (obj->data_segments != NULL)
        __rtld_state.libc_free((void *)obj->data_segments);

    if (obj->elem_segments != NULL)
        __rtld_state.libc_free((void *)obj->elem_segments);

    if (obj->filepath != NULL)
        __rtld_state.libc_free((void *)obj->filepath);

    if (obj->dso_name != NULL)
        __rtld_state.libc_free((void *)obj->dso_name);

    if (obj->dso_vers != NULL)
        __rtld_state.libc_free((void *)obj->dso_vers);

    if (obj->exechdr != NULL)
        __rtld_state.libc_free((void *)obj->exechdr);

    __rtld_state.libc_free(obj);
}

// early mapping (getting essential information in rtld.dylink.0 section)
struct wasm_module_rt*
_rtld_read_object_info(const char *filepath, int fd, struct stat *st, int flags)
{
    struct wasm_module_rt *obj;
    struct wasm_exechdr_secinfo *dylink0;
    struct wash_exechdr_rt *exehdr;
    uint32_t bufsz, wa_magic, wa_vers;
    uint32_t hdroff, hdrsz;
    uint32_t dylink0_off;
    char *buf;
    unsigned char *ptr;
    int ret;


    bufsz = 4096;
    buf = __rtld_state.libc_malloc(bufsz);
    if (obj == NULL) {
        dbg("%s ENOMEN for file-buf", __func__);
        return NULL;
    }

    obj = __rtld_state.libc_malloc(sizeof(struct wasm_module_rt));
    if (obj == NULL) {
        __rtld_state.libc_free(buf);
        dbg("%s ENOMEN for wasm_module_rt", __func__);
        return NULL;
    }

    wasm_memory_fill(obj, 0, sizeof(struct wasm_module_rt));

    obj->flags |= _RTLD_OBJ_MALLOC;
    obj->ld_dev = st->st_dev;
    obj->ld_ino = st->st_ino;
    obj->fd = fd;

    ptr = (uint8_t *)buf;

    // read magic + version and rtld.exec-hdr
    ret = __rtld_state.__sys_read(fd, buf, bufsz);
    if (ret == -1) {

    }

    if (*((uint32_t *)(ptr)) != WASM_HDR_SIGN_LE) {
        _rtld_error("Shared object \"%s\" invalid magic", filepath);
        goto error_out;
    }
    ptr += 4;

    if (*((uint32_t *)(ptr)) != 0x01) {
        _rtld_error("Shared object \"%s\" invalid version", filepath);
        goto error_out;
    }
    ptr += 4;

    hdroff = 0;
    hdrsz = 0;
    if (_rtld_has_exechdr_in_buf(ptr, bufsz - 8, &hdroff, &hdrsz) == false) {
        _rtld_error("Shared object \"%s\" missing rtld.exec-hdr", filepath);
        goto error_out;
    }

    if (hdroff == 0 || hdrsz == 0 || hdrsz >= bufsz) {
        _rtld_error("Shared object \"%s\" invalid rtld.exec-hdr", filepath);
        goto error_out;
    }

    exehdr = __rtld_state.libc_malloc(hdrsz);
    if (exehdr == NULL) {
        dbg("%s ENOMEN for exehdr", __func__);
        goto error_out;
    }

    exehdr->hdr_size = hdrsz;
    ret = _rtld_read_exechdr(ptr + hdroff, hdrsz, exehdr);
    if (ret != 0) {
        _rtld_error("Shared object \"%s\" invalid rtld.exec-hdr", filepath);
        goto error_out;
    }

    obj->exechdr = exehdr;
#if 0
    // before reading dylink0
    ret = _rtld_read_data_segments_info(obj, exehdr, fd, buf, bufsz);
    if (ret != 0) {
        dbg("Shared object \"%s\" error %d in data-segments", filepath, ret);
        goto error_out;
    }
#endif

    dylink0 = _rtld_exechdr_find_section(exehdr, WASM_SECTION_CUSTOM, "rtld.dylink.0");
    if (dylink0 == NULL) {
        _rtld_error("Shared object \"%s\" missing rtld.dylink.0", filepath);
        goto error_out;
    }

    dylink0_off = dylink0->file_offset;

    // read rtld.dylink.0
    ret = __rtld_state.__sys_lseek(fd, dylink0_off, SEEK_SET);
    ret = __rtld_state.__sys_read(fd, buf, bufsz);
    // read in data-segments outline from dylink.0 section (to later pre-estimate memory malloc size)
    // check modules which is needed first, use name + vers to match against already loaded modules.
    // malloc memory for all data-segments of all modules that is to be linked
    // malloc largest of filesize minus data-segments of that file
    ret = _rtld_read_dylink0_early(obj, exehdr, fd, buf, bufsz, dylink0_off);
    if (ret != 0) {
        dbg("%s got error = %d from _rtld_read_dylink0_early()", __func__, ret);
        goto error_out;
    }

    // getting absolute path
    if (PATH_MAX < bufsz) {
        char *str;
        int strsz, error;

        wasm_memory_fill(buf , 0, PATH_MAX + 1);
        error = __rtld_state.__sys_fcntl(fd, F_GETPATH, (long)buf);
        strsz = strnlen((const char *)buf, PATH_MAX);

        if (error != -1 && strsz > 0) {
            str = __rtld_state.libc_malloc(strsz + 1);
            if (str != NULL) {
                wasm_memory_copy(str, buf, strsz);
                str[strsz] = '\x00';

                obj->dso_pathlen = strsz;
                obj->filepath = str;
            } else {
                goto error_out;
            }
        }
    }

    dbg("%s finished for %s with obj = %p", __func__, obj->filepath, obj);

    return obj;

error_out:
    if (buf != NULL)
        __rtld_state.libc_free(buf);

    if (obj != NULL)
        _rtld_obj_free(obj);

    return NULL;
}

int
_rtld_map_memory_to_object(struct wasm_module_rt *obj)
{
    struct rtld_segment *dynsym;
    struct rtld_segment *dynstr;
    struct rtld_segment *pre_init_arr;
    struct rtld_segment *init_arr;
    struct rtld_segment *fnit_arr;
    char *addr;
    
    dynsym = _rtld_find_data_segment(obj, ".dynsym");
#if 0
    dynstr = _rtld_find_data_segment(obj, ".dynstr");
#endif
    pre_init_arr = _rtld_find_data_segment(obj, ".pre_init_array");
    init_arr = _rtld_find_data_segment(obj, ".init_array");
    fnit_arr = _rtld_find_data_segment(obj, ".fnit_array");

    if (dynsym) {
        addr = (char *)dynsym->addr;
        obj->dlsym_start = (void *)addr;
        obj->dlsym_end = (void *)(addr + dynsym->size);
    }

#if 0
    if (dynstr) {
        addr = (char *)dynstr->addr;
        obj->dlsym_start = (void *)addr;
        obj->dlsym_end = (void *)(addr + dynstr->size);
    }
#endif

    if (pre_init_arr) {
        addr = (char *)pre_init_arr->addr;
        obj->init_array = (void *)addr;
        obj->init_array_count = (pre_init_arr->size / sizeof(void *));
    }

    if (init_arr) {
        addr = (char *)init_arr->addr;
        obj->init_array = (void *)addr;
        obj->init_array_count = (init_arr->size / sizeof(void *));
    }

    if (fnit_arr) {
        addr = (char *)fnit_arr->addr;
        obj->init_array = (void *)addr;
        obj->init_array_count = (fnit_arr->size / sizeof(void *));
    }

    return (0);
}

int
_rtld_map_object(struct wasm_module_rt *obj, char *relocbuf, size_t relocbufsz, struct execbuf_mapping *mapping)
{
    struct execbuf_mapping *n_map, *c_map;
    uint32_t mapcnt, count;
    // 
    struct wash_exechdr_rt *hdr;
    struct wasm_exechdr_secinfo *sec;
    struct wasm_exechdr_secinfo *dylink0;
    struct execbuf_mapping *codesec_map;
    struct execbuf_mapping *importsec_map;
    struct execbuf_mapping *memorysec_map;
    struct execbuf_mapping dylink0_map;
    char *relocbuf_start;
    char *dylink0_p;
    char *codesec_p;
    char *impsec_p;
    char *memsec_p;
    uint32_t execbuf_offset, size;
    int fd, ret, error, objdesc;
    union {
        struct wasm_loader_cmd_mkbuf mkbuf;
        struct wasm_loader_cmd_wrbuf wrbuf;
        struct wasm_loader_cmd_compile_v2 run;
        struct wasm_loader_cmd_coredump dump;
    } exec_cmd;
    char tmperror[512];
    char tmpmagic[8];

    dbg("%s on object %s", __func__, obj->dso_name);

    // mapping execution buffer

    relocbuf_start = relocbuf;
    n_map = mapping;
    codesec_map = NULL;
    importsec_map = NULL;
    memorysec_map = NULL;
    execbuf_offset = 8; // since the signature + version is 8 bytes.
    hdr = obj->exechdr;
    mapcnt = 0;
    count = hdr->section_cnt;
    sec = hdr->secdata;
    for (int i = 0; i < count; i++) {
        c_map = NULL;
        if (sec->wasm_type == WASM_SECTION_CUSTOM) {
            if (sec->namesz == 4 &&  strncmp(sec->name, "name", 4) == 0) {
                c_map = n_map++;
                wasm_memory_fill(c_map, 0, sizeof(struct execbuf_mapping));
                mapcnt++;
            }
        } else if (sec->wasm_type == WASM_SECTION_CODE) {
            c_map = n_map++;
            wasm_memory_fill(c_map, 0, sizeof(struct execbuf_mapping));
            codesec_map = c_map;
            mapcnt++;

        } else if (sec->wasm_type == WASM_SECTION_IMPORT) {
            c_map = n_map++;
            wasm_memory_fill(c_map, 0, sizeof(struct execbuf_mapping));
            importsec_map = c_map;
            mapcnt++;

        } else if (sec->wasm_type == WASM_SECTION_MEMORY) {
            c_map = n_map++;
            wasm_memory_fill(c_map, 0, sizeof(struct execbuf_mapping));
            memorysec_map = c_map;
            mapcnt++;
        
        } else if (sec->wasm_type != WASM_SECTION_DATA && sec->wasm_type != WASM_SECTION_DATA_COUNT) {
            c_map = n_map++;
            wasm_memory_fill(c_map, 0, sizeof(struct execbuf_mapping));
            mapcnt++;
        }

        if (c_map != NULL) {
            c_map->sec = sec;
            c_map->dst_offset = execbuf_offset;
            execbuf_offset += c_map->sec->sec_size;
        }

        sec++;
    }

    dylink0 = _rtld_exechdr_find_section(hdr, WASM_SECTION_CUSTOM, "rtld.dylink.0");
    if (dylink0 == NULL) {
        _rtld_error("Shared object \"%s\" missing rtld.dylink.0", obj->filepath);
        //goto error_out;
    }

    //
    dylink0_p = NULL;
    impsec_p = NULL;
    memsec_p = NULL;
    codesec_p = NULL;

    wasm_memory_fill(&dylink0_map, 0, sizeof(struct execbuf_mapping));
    fd = obj->fd;
    dylink0_p = relocbuf;
    size = dylink0->sec_size;
    dylink0_map.addr = (uintptr_t)dylink0_p;
    dylink0_map.sec = dylink0;
    ret = __rtld_state.__sys_lseek(fd, dylink0->file_offset, SEEK_SET);
    ret = __rtld_state.__sys_read(fd, dylink0_p, size);
    if (ret == -1) {
        dbg("%s Error on read() errno = %d", __func__, errno);
    }
    relocbuf += size;

    if (importsec_map != NULL) {
        impsec_p = relocbuf;
        importsec_map->addr = (uintptr_t)impsec_p;
        sec = importsec_map->sec;
        size = sec->sec_size;
        ret = __rtld_state.__sys_lseek(fd, sec->file_offset, SEEK_SET);
        ret = __rtld_state.__sys_read(fd, impsec_p, size);
        if (ret == -1) {
            dbg("%s Error on read() errno = %d", __func__, errno);
        }
        relocbuf += size;
    }

    if (memorysec_map != NULL) {        
        memsec_p = relocbuf;
        memorysec_map->addr = (uintptr_t)memsec_p;
        sec = memorysec_map->sec;
        size = sec->sec_size;
        ret = __rtld_state.__sys_lseek(fd, sec->file_offset, SEEK_SET);
        ret = __rtld_state.__sys_read(fd, memsec_p, size);
        if (ret == -1) {
            dbg("%s Error on read() errno = %d", __func__, errno);
        }
        relocbuf += size;
    }

    if (codesec_map != NULL) {
        codesec_p = relocbuf;
        codesec_map->addr = (uintptr_t)codesec_p;
        sec = codesec_map->sec;
        size = sec->sec_size;
        ret = __rtld_state.__sys_lseek(fd, sec->file_offset, SEEK_SET);
        ret = __rtld_state.__sys_read(fd, codesec_p, size);
        if (ret == -1) {
            dbg("%s Error on read() errno = %d", __func__, errno);
        }

#if 0
        // core dump (remove later)
        exec_cmd.dump.objdesc = -1;
        exec_cmd.dump.filename = "code-section.txt";
        exec_cmd.dump.offset = (uint32_t)codesec_p;
        exec_cmd.dump.size = size;
        rtld_exec_ioctl(EXEC_IOCTL_COREDUMP, &exec_cmd);
#endif
        relocbuf += size;
    }

    // fixup memory descriptor so minimum & maximum matches objmain
    error = rtld_setup_memory_descriptors(obj, relocbuf_start, relocbufsz, importsec_map, memorysec_map);
    if (error != 0) {
        goto error_out;
    }

    // internal reloc
    error = rtld_do_internal_reloc_on_module(obj, relocbuf_start, relocbufsz, &dylink0_map, codesec_map);
    if (error != 0) {
        goto error_out;
    }

    // external reloc
    error = rtld_do_extern_reloc_on_module(obj, relocbuf_start, relocbufsz, &dylink0_map, codesec_map);
    if (error != 0) {
        goto error_out;
    }

    // create and map execution buffer
    
    exec_cmd.mkbuf.objdesc = -1;
    exec_cmd.mkbuf.size = execbuf_offset;
    error = rtld_exec_ioctl(EXEC_IOCTL_MKBUF, &exec_cmd);
    if (error != 0) {
        goto error_out;
    }
    objdesc = exec_cmd.mkbuf.objdesc;
    obj->obj_execdesc = objdesc;

    // first pass (copy just whats in memory into exec buf)
    c_map = mapping;
    for (int i = 0; i < mapcnt; i++) {
        if (c_map->addr != 0) {
            exec_cmd.wrbuf.objdesc = objdesc;
            exec_cmd.wrbuf.offset = c_map->dst_offset;
            exec_cmd.wrbuf.size = c_map->sec->sec_size;
            exec_cmd.wrbuf.src = (void *)c_map->addr;
            dbg("%s write buf start %d end %d (from mem) wasm-type = %d org-file-offset = %d\n", __func__, c_map->dst_offset, c_map->dst_offset + c_map->sec->sec_size, c_map->sec->wasm_type, c_map->sec->file_offset);
            error = rtld_exec_ioctl(EXEC_IOCTL_WRBUF, &exec_cmd);
            if (error != 0) {
                goto error_out;
            }
        }
        c_map++;
    }

    // writing wasm magic + version
    *((uint32_t *)(tmpmagic)) = WASM_HDR_SIGN_LE;
    *((uint32_t *)(tmpmagic + 4)) = 1;
    exec_cmd.wrbuf.objdesc = objdesc;
    exec_cmd.wrbuf.offset = 0;
    exec_cmd.wrbuf.size = 8;
    exec_cmd.wrbuf.src = &tmpmagic;
    error = rtld_exec_ioctl(EXEC_IOCTL_WRBUF, &exec_cmd);
    if (error != 0) {
        goto error_out;
    }

    // second pass everything that is keept as in the file, reuse anything in relocbuf for temp buffering
    uint32_t file_start;
    uint32_t dst_start;
    uint32_t read_size;
    file_start = 0;
    dst_start = 0;
    read_size = 0;
    c_map = mapping;
    for (int i = 0; i < mapcnt; i++) {
        bool read_now = true;
        if (c_map->addr == 0) {
            // TODO: block-align might yield better performance..
            
            if (read_size != 0) {
                read_size += c_map->sec->sec_size;
            } else {
                file_start = c_map->sec->file_offset;
                dst_start = c_map->dst_offset;
                read_size = c_map->sec->sec_size;
            }

            dbg("%s write buf start %d end %d wasm-type = %d org-file-offset = %d\n", __func__, c_map->dst_offset, c_map->dst_offset + c_map->sec->sec_size, c_map->sec->wasm_type, c_map->sec->file_offset);

            n_map = (i < mapcnt - 1) ? c_map + 1 : NULL;

            if (n_map && n_map->addr == 0 && (dst_start + size == n_map->dst_offset) && (file_start + size == n_map->sec->file_offset) && (read_size + n_map->sec->sec_size) < relocbufsz) {
                read_now = false;
            } else {
                read_now = true;
            }

            if (read_now) {

                ret = __rtld_state.__sys_lseek(fd, file_start, SEEK_SET);
                ret = __rtld_state.__sys_read(fd, relocbuf, read_size);
                if (ret == -1) {
                    dbg("%s Error on read() errno = %d", __func__, errno);
                }

                exec_cmd.wrbuf.objdesc = objdesc;
                exec_cmd.wrbuf.offset = dst_start;
                exec_cmd.wrbuf.size = read_size;
                exec_cmd.wrbuf.src = (void *)relocbuf;
                error = rtld_exec_ioctl(EXEC_IOCTL_WRBUF, &exec_cmd);
                if (error != 0) {
                    goto error_out;
                }
                file_start = 0;
                dst_start = 0;
                read_size = 0;

            }
        }
        c_map++;
    }

    wasm_memory_fill(&exec_cmd, 0, sizeof(exec_cmd));
    exec_cmd.run.objdesc = objdesc;
    exec_cmd.run.__dso_handle = (uintptr_t)obj;
    exec_cmd.run.errmsg = tmperror;
    exec_cmd.run.errmsgsz = sizeof(tmperror);
    error = rtld_exec_ioctl(EXEC_IOCTL_COMPILE, &exec_cmd);
    if (error != 0) {
        goto error_out;
    }

    __rtld_state.__sys_close(fd);
    obj->fd = -1;

    return (0);

error_out:

    __rtld_state.__sys_close(fd);
    obj->fd = -1;

    return error;

#if 0
    struct wash_exechdr_rt *hdr;
    struct wasm_exechdr_secinfo *sec;
    struct wasm_exechdr_secinfo *dylink0;
    struct rtld_segment *elem, *data;
    struct section_copy_map *map, *head, *tail, *codemap, *importmap, *memorymap;
    uint32_t mapcnt;
    uint32_t mem_off, mem_size;
    char *membase;
    char *dylink0_p;
    char *code_p;
    char *impsec_p;
    char *memory_p;
    uint32_t tbl_off, tbl_size;
    uint32_t execbuf_offset;
    uint32_t count;
    int fd, ret, error;

    // first off map which sections is to be handled to WebAssembly.Compile() this so we can map what
    // is to be loaded from file and what is to be copied from memory.
    head = NULL;
    tail = NULL;
    codemap = NULL;
    importmap = NULL;
    memorymap = NULL;
    execbuf_offset = 0;
    hdr = obj->exechdr;
    count = hdr->section_cnt;
    sec = hdr->secdata;
    for (int i = 0; i < count; i++) {
        map = NULL;
        if (sec->wasm_type == WASM_SECTION_CUSTOM) {
            if (sec->namesz == 4 &&  strncmp(sec->name, "name", 4) == 0) {
                map = __rtld_state.libc_malloc(sizeof(struct section_copy_map));
                wasm_memory_fill(map, 0, sizeof(struct section_copy_map));
                mapcnt++;
            }
        } else if (sec->wasm_type == WASM_SECTION_CODE) {
            map = __rtld_state.libc_malloc(sizeof(struct section_copy_map));
            wasm_memory_fill(map, 0, sizeof(struct section_copy_map));
            codemap = map;
            mapcnt++;

        } else if (sec->wasm_type == WASM_SECTION_IMPORT) {
            map = __rtld_state.libc_malloc(sizeof(struct section_copy_map));
            wasm_memory_fill(map, 0, sizeof(struct section_copy_map));
            importmap = map;
            mapcnt++;

        } else if (sec->wasm_type == WASM_SECTION_MEMORY) {
            map = __rtld_state.libc_malloc(sizeof(struct section_copy_map));
            wasm_memory_fill(map, 0, sizeof(struct section_copy_map));
            memorymap = map;
            mapcnt++;
        
        } else if (sec->wasm_type != WASM_SECTION_DATA && sec->wasm_type != WASM_SECTION_DATA_COUNT) {
            map = __rtld_state.libc_malloc(sizeof(struct section_copy_map));
            wasm_memory_fill(map, 0, sizeof(struct section_copy_map));
            mapcnt++;
        }

        if (map != NULL) {
            map->sec = sec;
            map->dst_offset = execbuf_offset;
            execbuf_offset += map->sec->sec_size;
            if (head == NULL)
                head = map;
            if (tail == NULL) {
                tail = map;
            } else {
                tail->next = map;
                tail = map;
            }
        }

        sec++;
    }

    fd = __rtld_state.__sys_open(obj->filepath, O_RDONLY | O_REGULAR, 0);
    if (fd == -1) {
        error = errno;
        dbg("%s Error on open() errno = %d", __func__, errno);
        goto error_out;
    }

    tbl_off = 0;
    mem_off = 0;
    elem = obj->elem_segments;
    count = obj->elem_segments_count;

    for (int i = 0; i < count; i++) {
        if (tbl_off != 0 && elem->align != 0)
            tbl_off = alignUp(tbl_off, elem->align, NULL);
        tbl_off += elem->size;
        elem++;
    }

    data = obj->data_segments;
    count = obj->data_segments_count;
    
    for (int i = 0; i < count; i++) {
        if (mem_off != 0 && data->align != 0)
            mem_off = alignUp(mem_off, data->align, NULL);
        mem_off += data->size;
        data++;
    }

    mem_size = mem_off;
    mem_off = 0;

    dbg("%s needed table space = %d", __func__, tbl_off);
    dbg("%s needed memory space = %d", __func__, mem_off);

    membase = __rtld_state.libc_malloc(mem_size);
    obj->membase = (uintptr_t)membase;
    obj->memsize = mem_size;

    tbl_size = tbl_off;
    ret = wasm_table_grow(tbl_size);
    if (ret == -1) {
        error = E2BIG;
        _rtld_error("Shared object \"%s\" cannot grow table.0", obj->filepath);
        goto error_out;
    }

    count = obj->data_segments_count;
    data = obj->data_segments;
    for (int i = 0; i < count; i++) {
        if (membase != NULL && data->align != 0)
            membase = (void *)alignUp((uintptr_t)membase, data->align, NULL);
        ret = __rtld_state.__sys_read(fd, membase, data->size);
        if (ret == -1) {
            dbg("%s Error on read() errno = %d", __func__, errno);
        }
        data->addr = (uintptr_t)membase;
        membase += data->size;
        data++;
    }

    dylink0 = _rtld_exechdr_find_section(hdr, WASM_SECTION_CUSTOM, "rtld.dylink.0");
    if (dylink0 == NULL) {
        _rtld_error("Shared object \"%s\" missing rtld.dylink.0", obj->filepath);
        //goto error_out;
    }
    
    dylink0_p = relocbuf;
    ret = __rtld_state.__sys_read(fd, dylink0_p, dylink0->sec_size);
    if (ret == -1) {
        dbg("%s Error on read() errno = %d", __func__, errno);
    }

    if (importmap != NULL) {
        relocbuf += dylink0->sec_size;
        import_p = relocbuf;
        importmap->addr = (uintptr_t)import_p;
        ret = __rtld_state.__sys_read(fd, import_p, importmap->sec->sec_size);
        if (ret == -1) {
            dbg("%s Error on read() errno = %d", __func__, errno);
        }
        relocbuf += importmap->sec->sec_size;
    }

    if (memorymap != NULL) {        
        memory_p = relocbuf;
        memorymap->addr = (uintptr_t)memory_p;
        ret = __rtld_state.__sys_read(fd, memory_p, memorymap->sec->sec_size);
        if (ret == -1) {
            dbg("%s Error on read() errno = %d", __func__, errno);
        }
        relocbuf += memorymap->sec->sec_size;
    }

    if (codemap != NULL) {
        code_p = relocbuf;
        codemap->addr = (uintptr_t)code_p;
        ret = __rtld_state.__sys_read(fd, code_p, codemap->sec->sec_size);
        if (ret == -1) {
            dbg("%s Error on read() errno = %d", __func__, errno);
        }

        relocbuf += importmap->sec->sec_size;
    }

    // internal reloc

    // external reloc

    __rtld_state.__sys_close(fd);

    for (map = head; map != NULL; map = map->next) {
        __rtld_state.libc_free(map);
    }

    return (0);

error_out: 

    __rtld_state.__sys_close(fd);

    __rtld_state.libc_free(membase);

    for (map = head; map != NULL; map = map->next) {
        __rtld_state.libc_free(map);
    }

    return error;
#endif
}

/**
 * late stage mapping, until now rtld have only been loading data to detetermine the needs for all objects to be loaded
 */
int 
_rtld_map_objects(struct wasm_module_rt *first)
{
    // first of allocate buffer for reloc of code and WebAssembly.Memory (import/memory section)
    struct wasm_module_rt *obj;
    struct wasm_exechdr_secinfo *sec;
    struct wash_exechdr_rt *hdr;
    struct rtld_segment *elem, *data;
    struct execbuf_mapping *execbufmap;
    char *relocbuf;                  // temporary memory for reloc every code section one at a time
    char *membase;
    uint32_t obj_reloc_size;
    uint32_t max_reloc_size;    // needed temporary buffer to hold non memory relocation in user-space memory
    uint32_t max_align;
    uint32_t max_sec_count;
    uint32_t mapcnt;
    uint32_t mem_off, mem_size;
    uint32_t tbl_off, tbl_size;
    uint32_t count;
    int32_t tblbase;
    int fd, ret, error;

    relocbuf = NULL;
    max_reloc_size = 0;
    max_sec_count = 0;

    for (obj = first; obj != NULL; obj = obj->next) {

        obj_reloc_size = 0;
        hdr = obj->exechdr;
        if (hdr == NULL) {
            dbg("%s obj->exechdr is NULL for %s", __func__, obj->dso_name);
            return -1;
        }

        sec = _rtld_exechdr_find_section(hdr, WASM_SECTION_MEMORY, NULL);
        if (sec != NULL)
            obj_reloc_size += sec->sec_size; // whole section size

        sec = _rtld_exechdr_find_section(hdr, WASM_SECTION_IMPORT, NULL);
        if (sec != NULL)
            obj_reloc_size += sec->sec_size;

        sec = _rtld_exechdr_find_section(hdr, WASM_SECTION_CODE, NULL);
        if (sec != NULL)
            obj_reloc_size += sec->sec_size;
        
        sec = _rtld_exechdr_find_section(hdr, WASM_SECTION_CUSTOM, "rtld.dylink.0");
        if (sec != NULL)
            obj_reloc_size += sec->sec_size;

        if (obj_reloc_size > max_reloc_size) {
            max_reloc_size = obj_reloc_size;
        }

        if (hdr->section_cnt > max_sec_count) {
            max_sec_count = hdr->section_cnt;
        }
    }

    // TODO: It might be better to allocate relocbuf mem after data memory, since then it directly below the brk() which
    //       would allow a greater segment to be allocated elsewhere.

    execbufmap = __rtld_state.libc_malloc(max_sec_count * sizeof(struct execbuf_mapping));
    if (execbufmap == NULL) {
        error = ENOMEM;
        dbg("%s ENOMEM for execbuf mapping", __func__);
        goto error_out;
    }

    dbg("%s max reloc needs = %d buf @+%lx", __func__, max_reloc_size, (uintptr_t)relocbuf);

    // first pass determine needs for intial memory
    for (obj = first; obj != NULL; obj = obj->next) {

        data = obj->data_segments;
        count = obj->data_segments_count;
        mem_off = 0;
        mem_size = 0;
        
        for (int i = 0; i < count; i++) {
            mem_size += (data->size + data->align);
            data++;
        }

        membase = __rtld_state.libc_malloc(mem_size);
        if (membase == NULL) {
            error = ENOMEM;
            dbg("%s ENOMEM for initial memory of size %d for object = %s", __func__, mem_size, obj->dso_name);
            goto error_out;
        }
        mem_off = (uint32_t)membase;
        obj->membase = (uintptr_t)membase;

        data = obj->data_segments;
        for (int i = 0; i < count; i++) {
            if (mem_off != 0 && data->align != 0)
                mem_off = alignUp(mem_off, data->align, NULL);
            data->addr = mem_off;
            mem_off += data->size;
            dbg("%s placing %s %s of size = %d at addr = %d\n", __func__, obj->dso_name, data->name, data->size, data->addr);
            data++;
        }

        if (mem_off > (uint32_t)(membase + mem_size)) {
            dbg("alloc memory is too small");
        }
    }

    relocbuf = __rtld_state.libc_malloc(max_reloc_size);
    if (relocbuf == NULL) {
        error = ENOMEM;
        dbg("%s ENOMEM for max_reloc_size = %d", __func__, max_reloc_size);
        goto error_out;
    }

    // secound pass copy/initialize memory
    for (obj = first; obj != NULL; obj = obj->next) {

        fd = obj->fd;
        data = obj->data_segments;
        count = obj->data_segments_count;
        
        for (int i = 0; i < count; i++) {

            if ((data->flags & _RTLD_SEGMENT_ZERO_FILL) != 0) {
                wasm_memory_fill((void *)data->addr, 0, data->size);
                data++;
                continue;
            }

            ret = __rtld_state.__sys_lseek(fd, data->src_offset, SEEK_SET);
            ret = __rtld_state.__sys_read(fd, (void *)data->addr, data->size);
            if (ret == -1) {
                error = errno;
                dbg("%s Error on read() errno = %d", __func__, error);
                goto error_out;
            }
            data++;
        }

        // map where memory segment where place
        error = _rtld_map_memory_to_object(obj);
        if (error != 0) {
            dbg("%s Error on _rtld_map_memory_to_object() errno = %d", __func__, error);
            goto error_out;
        }
    }

    // reserve space in __indirect_table
    for (obj = first; obj != NULL; obj = obj->next) {

        elem = obj->elem_segments;
        count = obj->elem_segments_count;
        tbl_size = 0;
        
        for (int i = 0; i < count; i++) {
            tbl_size += (elem->size + elem->align);
            elem++;
        }

        tblbase = wasm_table_grow(tbl_size);
        if (tblbase == -1) {
            error = ENOMEM;
            dbg("%s ENOMEM for resavation of size = %d on __indirect_table for obj->name = %s", __func__, tbl_size, obj->dso_name);
            goto error_out;
        }

        tbl_off = tblbase;
        elem = obj->elem_segments;
        for (int i = 0; i < count; i++) {
            if (tbl_off != 0 && elem->align != 0)
                tbl_off = alignUp(tbl_off, elem->align, NULL);
            elem->addr = tbl_off;
            tbl_off += elem->size;
            elem++;
        }
    }

    // new do the reloc that depends on internal element-segment & data-segments
    // by doing this now circular dependencies are possible to lookup.
    for (obj = first; obj != NULL; obj = obj->next) {

        fd = obj->fd;
        if (obj->exechdr == NULL || fd == -1) {
            dbg("%s could not find exechdr on object for internal data reloc..", __func__);
            continue;
        }

        sec = _rtld_exechdr_find_section(obj->exechdr, WASM_SECTION_CUSTOM, "rtld.dylink.0");
        if (sec == NULL) {
            dbg("%s could not find dylink.0 for internal data reloc..", __func__);
            continue;
        }

        
        ret = __rtld_state.__sys_lseek(fd, sec->file_offset, SEEK_SET);
        ret = __rtld_state.__sys_read(fd, relocbuf, sec->sec_size);
        if (ret == -1) {
            error = errno;
            dbg("%s Error on read() errno = %d", __func__, error);
            goto error_out;
        }

        // map where memory segment where place
        error = _rtld_do_internal_data_reloc(obj, relocbuf, max_reloc_size, relocbuf, relocbuf + sec->sec_size);
        if (error != 0) {
            dbg("%s Error on _rtld_do_internal_data_reloc() errno = %d", __func__, error);
            goto error_out;
        }
    }

    // now when initial memory is in memory, load each object at a time
    for (obj = first; obj != NULL; obj = obj->next) {

        error = _rtld_map_object(obj, relocbuf, max_reloc_size, execbufmap);
        if (error != 0) {
            dbg("%s Error on _rtld_map_object() errno = %d", __func__, error);
            goto error_out;
        }
    }

    return (0);

error_out:

    // clean up allocated memory
    for (obj = first; obj != NULL; obj = obj->next) {
        membase = (char *)obj->membase;
        if (membase != NULL)
            __rtld_state.libc_free(membase);
    }

    if (relocbuf != NULL)
        __rtld_state.libc_free(relocbuf);

    dbg("%s error_out with error %d", __func__, error);

    return error;
}

#define LIBNAMEBUFSZ NAME_MAX + VERS_MAX + 1

int
_rtld_load_by_name(const char *name, const char *vers, struct wasm_module_rt *obj, struct _rtld_needed_entry **needed, int flags)
{
    struct wasm_module_rt *res;
    char tmpname[LIBNAMEBUFSZ];
    char *libp;
    uint32_t namesz = (*needed)->name == name ? (*needed)->namesz : strlen(name);

    dbg("load by name %s", name);
    res = _rtld_find_dso_handle_by_name(name, vers);
    if (res) {
        (*needed)->obj = res;
        return true;
    }

    // native rtld does check sysctl for library defintions here..

    // for wasm we test a variation of string combos
    // {NAME}.so.{VERS}          (if not ends with .so the extension is added
    // lib{NAME}.so.{VERS}       (if not start or ends with "lib")

    // merge the module name into somehting that similar to unix convention for libraries.
    res = _rtld_load_library(NULL, name, obj, flags);
    if (res == NULL) {
        
        if (vers != NULL) {
            sprintf(tmpname, LIBNAMEBUFSZ, "%s%s%s", name, ".so.", vers);
        } else {
            sprintf(tmpname, LIBNAMEBUFSZ, "%s%s", name, ".so");
        }

        res = _rtld_load_library(NULL, tmpname, obj, flags);

        
        // another try with lib inserted at start if the name does not already start or ends with lib
        if (res == NULL) {
            libp = strstr(name, "lib");
            if (libp != name && libp != name + (namesz - 3)) {
                if (vers != NULL) {
                    sprintf(tmpname, LIBNAMEBUFSZ, "lib%s%s", name, ".so.", vers);
                } else {
                    sprintf(tmpname, LIBNAMEBUFSZ, "lib%s%s", name, ".so");
                }
                res = _rtld_load_library(NULL, tmpname, obj, flags);
            }
        }
    }

    (*needed)->obj = res;

    return res != NULL;
}

int
_rtld_load_needed_objects(struct wasm_module_rt *first, int flags)
{
    struct wasm_module_rt *obj;
    struct _rtld_needed_entry *needed;
    uint32_t result, status = 0;

    for (obj = first; obj != NULL; obj = obj->next) {

        for (needed = obj->needed; needed != NULL; needed = needed->next) {
            
            result = _rtld_load_by_name(needed->name, needed->vers, obj, &needed, flags);
            if (!result) {
                status = -1;
            }
        }
    }

    if (status != -1) {
        
        result = _rtld_map_objects(first);

         //_rtld_map_object(const char *filepath, int fd, struct stat *st, int flags)
        
    }

    return status;
}

struct wasm_module_rt*
_rtld_load_object(const char *filepath, int flags)
{
    struct wasm_module_rt *obj, *tmp;
    struct stat sbuf;
    int count;
    int pathlen;
    int fd, ret, error;

    pathlen = strlen(filepath);

    // it might be possible to match by just the filepath, but its a longshot.
    for (obj = __rtld_state.rtld.objlist; obj != NULL; obj = obj->next) {
        if (obj->filepath != NULL && obj->dso_pathlen && pathlen == obj->dso_pathlen && strncmp(obj->filepath, filepath, pathlen) == 0) {
            dbg("%s found %p for %s by fullpath match", __func__, obj, filepath);
            break;
            // simply break, if we get to the end obj will be NULL anyways.
        }
    }

    // its possible that the filepath is not a direct match even if it was what dlopen was invoked with,
    // due to double slashes, symbolic links, and mount points etc. to avoid implementing namei subrutine
    // we open the filepath and compare fsid and fileid of the file-system with the link object.
    if (obj == NULL) {
        fd = __rtld_state.__sys_open(filepath, O_RDONLY | O_REGULAR, 0);
        if (fd == -1) {
            error = errno;
            _rtld_error("Cannot open \"%s\" errno = %d", filepath, error);
            return NULL;
        }

        wasm_memory_fill(&sbuf, 0, sizeof(sbuf));
        ret = __rtld_state.__sys_fstat(fd, &sbuf);
        if (ret == -1) {
            error = errno; 
            _rtld_error("Cannot fstat \"%s\" errno = %d", filepath, error);
			__rtld_state.__sys_close(fd);
			return NULL;
        }

        for (obj = __rtld_state.rtld.objlist; obj != NULL; obj = obj->next) {
            if (obj->ld_ino == sbuf.st_ino && obj->ld_dev == sbuf.st_dev) {
                __rtld_state.__sys_close(fd);
                dbg("%s found %p (object->name = %s) for %s by inode num match %llu == %llu", __func__, obj, obj->dso_name, filepath, obj->ld_ino, sbuf.st_ino);
                break;
            }
        }
    }

    // if object was found or no loading should take place, return here.
    if (obj != NULL || (flags & RTLD_NOLOAD) != 0) {
        return obj;
    }

    obj = _rtld_read_object_info(filepath, fd, &sbuf, flags);
    //__rtld_state.__sys_close(fd);   // TODO: might be better to keep the file open?
    if (obj) {
        tmp = __rtld_state.rtld.objtail;
        if (tmp != NULL)
            tmp->next = obj;
        __rtld_state.rtld.objtail = obj;
        __rtld_state.rtld.objcount++;
        __rtld_state.rtld.objloads++;
    }

    return obj;
}

#if 0
int 
_rtld_load_dso_library(const char *filepath, struct wasm_module_rt **module)
{
    struct stat st_buf;
    char *buf;
    uint32_t dylink0_off;
    uint32_t filesz, bufsz;
    int fd, ret;

    fd = __rtld_state.__sys_open(filepath, FREAD | O_REGULAR, 0);
    if (fd == -1) {
        if (errp)
            *errp = errno;
        return NULL;
    }

    wasm_memory_fill(&st_buf, 0, sizeof(struct stat));
    ret = __rtld_state.__sys_fstat(fd, &st_buf);
    if (ret == -1) {
        if (errp)
            *errp = errno;
        return NULL;   
    }

    module = __rtld_state.libc_malloc(sizeof(struct wasm_module_rt));
    wasm_memory_fill(&module, 0, sizeof(struct wasm_module_rt));

    filesz = st_buf.st_size;
    bufsz = 4096;
    buf = __rtld_state.libc_malloc(bufsz);
    if (buf == NULL) {
        if (errp)
            *errp = ENOMEM;
        return NULL;
    }

    // read rtld.exec-hdr
    ret = __rtld_state.__sys_read(fd, buf, bufsz);


    // read rtld.dylink.0
    ret = __rtld_state.__sys_lseek(fd, dylink0_off, 1);
    // read in data-segments outline from dylink.0 section (to later pre-estimate memory malloc size)
    // check modules which must be loaded first
    // malloc data-segments * [modules now linking]
    // malloc largest of (filesize minus data-segments of that file)

    return NULL;
}
#endif
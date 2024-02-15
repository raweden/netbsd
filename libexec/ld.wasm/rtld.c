

#include <sys/errno.h>
#include <sys/stdint.h>
#include <sys/stdbool.h>

#include <dlfcn.h>

#include "arch/wasm/include/wasm_module.h"
#include "stdlib.h"
#include "rtld.h"

//#include <libwasm/wasmloader.h>
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
void wasm_table_zerofill(int tableidx, int dst, int len) __WASM_BUILTIN(table_zerofill);

struct rtld_state {
    struct rtld_state_common rtld;
    // for dynamic loading after entering main() and when memory.grow is not a option.
    void *(*libc_malloc)(unsigned long);
    void (*libc_free)(void *);
    // in order to load & link the runtime-linker needs syscall access, this before libc is loaded.
    int (*__sys_open)(const char *filepath, int flags, long arg);
    int (*__sys_close)(int fd);
    int64_t (*__sys_lseek)(int fd, int64_t offset, int whence);
    int (*__sys_getdents)(int fd, char *buf, unsigned long count);
    long (*__sys_read)(int fd, void *buf, unsigned long nbyte);
    long (*__sys_write)(int fd, const void *buf, unsigned long nbyte);
    int (*__sys_fstat)(int fd, struct stat *sb);
    int (*__sys_lstat)(const char *path, struct stat *ub);
    ssize_t (*__sys_readlink)(const char *path, char *buf, size_t count);
    int (*__sys_fcntl)(int fd, int cmd, long arg);
};

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
int _rtld_link_against_libc(struct wasm_module_rt *libc_dso);
int __dlsym_early(struct dlsym_rt * restrict start, struct dlsym_rt * restrict end, unsigned int namesz, const char * restrict name, unsigned char type);

void _rtld_mutex_enter(uint32_t *ld_mutex)
{
    uint32_t cnt;
    uint32_t val;
    cnt = 0;
    val = atomic_cmpxchg32(ld_mutex, 0, 1);
    while (val != 0) {
        if (cnt >= 10000) {
            __panic_abort();
        }
        val = atomic_cmpxchg32(ld_mutex, 0, 1);
    }
}

int _rtld_mutex_try_enter(uint32_t *ld_mutex)
{
    uint32_t val;
    val = atomic_cmpxchg32(ld_mutex, 0, 1);
    if (val == 0) {
        return true;
    }

    return false;
}

void _rtld_mutex_exit(uint32_t *ld_mutex)
{
    uint32_t val;
    val = atomic_cmpxchg32(ld_mutex, 1, 0);
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
}

#if 0
__attribute__((destructor))
void 
__rtld_fnit(void) 
{

}
#endif

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
_rtld_alloc_filepath(int fd, struct wasm_module_rt *module)
{
    char tmpbuf[PATH_MAX];
    char *str;
    int strsz, error;

    wasm_memory_fill(&tmpbuf, 0, PATH_MAX);
    error = __rtld_state.__sys_fcntl(fd, F_GETPATH, (long)&tmpbuf);
    if (error) {
        return error;
    }
    strsz = strnlen((const char *)&tmpbuf, PATH_MAX);
    str = __rtld_state.libc_malloc(strsz + 1);
    if (str == NULL) {
        return ENOMEM;
    }
    wasm_memory_copy(str, tmpbuf, strsz);
    str[strsz] = '\x00';

    module->filepath = str;

    return 0;
}

int
_rtld_unload_object(struct wasm_module_rt *dso)
{
    if (dso->ld_refcount == 0) {
        dso->ld_state = _RTLD_UNLOADING;
    }

    return (0);
}

int
_rtld_do_substitution(struct wasm_module_rt *dso_self, const char *filepath, char *pathbuf)
{
    // $ORIGIN (or equivalently ${ORIGIN})
    // $LIB (or equivalently ${LIB}
    // $PLATFORM (or equivalently ${PLATFORM})


    return (0);
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
    struct wasm_module_rt *mod, *dso;
    bool nodelete;
    bool isfilename;
    char *pathbuf;
    struct stat sbuf;
    int error;

    if (__rtld_state.rtld.ld_state == RTLD_STATE_UNINIT) {
        __rtld_init();
    }

    // return handle for the main program
    if (filepath == NULL) {
        dso = __rtld_state.rtld.main_obj;
        dso->ld_refcount++;
    } else {
        dso = _rtld_load_library(dso_self, filepath, __rtld_state.rtld.main_obj, flags);
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


// searching 

struct wasm_module_rt*
_rtld_search_library_path(const char *name, size_t namelen, const char *dir, size_t dirlen, int flags)
{
    struct wasm_module_rt *dso;
    struct _rtld_search_path *sp;
    char pathname[PATH_MAX];
    size_t pathnamelen;
    int error;

    dso = NULL;
    pathnamelen = dirlen + 1 + namelen;
    if (pathnamelen >= sizeof(pathname)) {
        return NULL;
    }

    // The elf rtld also stores and check all path that it failed to link.

    wasm_memory_copy(pathname, dir, dirlen);
    pathname[dirlen] = '/';
    wasm_memory_copy(pathname + dirlen + 1, name, namelen);
    pathname[pathnamelen] = '\0';

    dbg("  Trying \"%s\"", pathname);
    dso = _rtld_load_object(pathname, flags);

    return dso;
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
    int error;

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
        for (sp = dso_self->rpaths; sp != NULL; sp = sp->sp_next) {
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
    if (error == 0) {
        return dso;
    }

    return NULL;
}

// reading netbsd.exec-hdr

/**
 * Returns a boolean true if a `netbsd.exec-hdr` custom section seams to be provided in the given buffer.
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

    if (namesz != 15 || strncmp((char *)ptr, "netbsd.exec-hdr", 15) != 0) {
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

// loading

static const struct {
    uint8_t namesz;
    const char *name;
} _rtld_common_names[] = {
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

}

#define DYLINK_SUBSEC_HEADING 1

int
_rtld_read_dylink0_early(struct wasm_module_rt *obj, struct wash_exechdr_rt *exechdr, int fd, const char *buf, uint32_t bufsz, uint32_t file_offset)
{
    struct wasm_exechdr_secinfo *data_sec;
    struct rtld_segment *seg, *elem_segments, *data_segments;
    char tmperr[128];
    const char *errstr;
    uint32_t lebsz, secsz, namesz, verssz;
    uint32_t subcnt, subsz, cnt, max_count;
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
        dbg("%s not a netbsd.dylink.0 section..\n", __func__);
        return ENOENT;
    }

    ptr++;
    secsz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;
    if (namesz != 15 || strncmp((const char *)ptr, "netbsd.dylink.0", namesz) != 0) {
        dbg("%s not a netbsd.dylink.0 section..\n", __func__);
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
    cnt = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    for (int i = 0; i < cnt; i++) {

        namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (namesz != 0)
            ptr += namesz;
        if (namesz > NAME_MAX) {
            error = ENAMETOOLONG;
            goto error_out;
        }


        verssz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (verssz != 0)
            ptr += verssz;
        if (verssz > VERS_MAX) {
            error = ENAMETOOLONG;
            goto error_out;
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
        wasm_memory_fill(elem_segments, 0, sizeof(struct rtld_segment) * cnt);

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
            seg->src_offset = dataoff;

            dbg("%s index = %d name %s size = %d align = %d data-offset = %d\n", __func__, i, seg->name, (uint32_t)seg->size, seg->align, seg->src_offset);
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
    
    // elem_segments, data_segments if allocated are freed using _rtld_free_object_data()

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
_rtld_free_object_data(struct wasm_module_rt*obj)
{
    if ((obj->flags & _RTLD_OBJ_MALLOC) == 0) {
        return;
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

    __rtld_state.libc_free(obj);
}

struct wasm_module_rt*
_rtld_map_object(const char *filepath, int fd, struct stat *st, int flags)
{
    struct wasm_module_rt *dso;
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
    if (dso == NULL) {
        dbg("%s ENOMEN for file-buf", __func__);
        return NULL;
    }

    dso = __rtld_state.libc_malloc(sizeof(struct wasm_module_rt));
    if (dso == NULL) {
        __rtld_state.libc_free(buf);
        dbg("%s ENOMEN for wasm_module_rt", __func__);
        return NULL;
    }

    wasm_memory_fill(dso, 0, sizeof(struct wasm_module_rt));

    dso->flags |= _RTLD_OBJ_MALLOC;
    dso->ld_dev = st->st_dev;
    dso->ld_ino = st->st_ino;

    ptr = (uint8_t *)buf;

    // read magic + version and netbsd.exec-hdr
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
        _rtld_error("Shared object \"%s\" missing netbsd.exec-hdr", filepath);
        goto error_out;
    }

    if (hdroff == 0 || hdrsz == 0 || hdrsz >= bufsz) {
        _rtld_error("Shared object \"%s\" invalid netbsd.exec-hdr", filepath);
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
        _rtld_error("Shared object \"%s\" invalid netbsd.exec-hdr", filepath);
        goto error_out;
    }

#if 0
    // before reading dylink0
    ret = _rtld_read_data_segments_info(dso, exehdr, fd, buf, bufsz);
    if (ret != 0) {
        dbg("Shared object \"%s\" error %d in data-segments", filepath, ret);
        goto error_out;
    }
#endif

    dylink0 = _rtld_exechdr_find_section(exehdr, WASM_SECTION_CUSTOM, "netbsd.dylink.0");
    if (dylink0 == NULL) {
        _rtld_error("Shared object \"%s\" missing netbsd.dylink.0", filepath);
        goto error_out;
    }

    dylink0_off = dylink0->file_offset;

    // read netbsd.dylink.0
    ret = __rtld_state.__sys_lseek(fd, dylink0_off, SEEK_SET);
    ret = __rtld_state.__sys_read(fd, buf, bufsz);
    // read in data-segments outline from dylink.0 section (to later pre-estimate memory malloc size)
    // check modules which is needed first, use name + vers to match against already loaded modules.
    // malloc memory for all data-segments of all modules that is to be linked
    // malloc largest of filesize minus data-segments of that file
    _rtld_read_dylink0_early(dso, exehdr, fd, buf, bufsz, dylink0_off);

    //return NULL;

error_out:
    if (buf != NULL)
        __rtld_state.libc_free(buf);

    if (dso != NULL)
        _rtld_free_object_data(dso);

    if (exehdr != NULL) {
        __rtld_state.libc_free(exehdr);
    }

    return NULL;
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
        if (obj->filepath != NULL && pathlen == obj->dso_pathlen && strncmp(obj->filepath, filepath, pathlen) == 0) {
            break;
            // simply break, if we get to the end obj will be NULL anyways.
        }
    }

    // its possible that the filepath is not a direct match even if it was what dlopen was invoked with,
    // due to double slashes, symbolic links, and mount points etc. to avoid implementing namei subrutine
    // we open the filepath and compare fsid and fileid of the file-system with the link object.
    if (obj == NULL) {
        fd = __rtld_state.__sys_open(filepath, O_RDONLY, 0);
        if (fd == -1) {
            error = errno;
            _rtld_error("Cannot open \"%s\" errno = %d", filepath, error);
            return NULL;
        }

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
                break;
            }
        }
    }

    dbg("%s found %p for %s", __func__, obj, filepath);

    // if object was found or no loading should take place, return here.
    if (obj != NULL || (flags & RTLD_NOLOAD) != 0) {
        return obj;
    }

    obj = _rtld_map_object(filepath, fd, &sbuf, flags);
    __rtld_state.__sys_close(fd);
    if (obj) {
        tmp = __rtld_state.rtld.obj_tail;
        if (tmp != NULL)
            tmp->next = obj;
        __rtld_state.rtld.obj_tail = obj;
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

    // read netbsd.exec-hdr
    ret = __rtld_state.__sys_read(fd, buf, bufsz);


    // read netbsd.dylink.0
    ret = __rtld_state.__sys_lseek(fd, dylink0_off, 1);
    // read in data-segments outline from dylink.0 section (to later pre-estimate memory malloc size)
    // check modules which must be loaded first
    // malloc data-segments * [modules now linking]
    // malloc largest of (filesize minus data-segments of that file)

    return NULL;
}
#endif
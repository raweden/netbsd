
#ifndef __RTLD_WASM_INTERNAL_H_
#define __RTLD_WASM_INTERNAL_H_

#include <sys/errno.h>
#include <sys/stdint.h>

#include <stdbool.h>

#include "rtsys.h"

#include <arch/wasm/include/wasm_module.h>

#ifndef IN_RTLD
#define IN_RTLD 1
#endif

// common macros
#ifndef roundup2
#define	roundup2(x,m)	((((x) - 1) | ((m) - 1)) + 1)
#endif
#ifndef rounddown2
#define	rounddown2(x,m)	((x) & ~((__typeof__(x))((m) - 1)))
#endif

#ifndef howmany
#define	howmany(x, y)	(((x)+((y)-1))/(y))
#endif

#ifndef __noinline
#define	__noinline	__attribute__((__noinline__))
#endif

// debug log

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

// rtld.c
void _rtld_mutex_enter(uint32_t *ld_mutex);
int _rtld_mutex_try_enter(uint32_t *ld_mutex);
void _rtld_mutex_exit(uint32_t *ld_mutex);

// rtld.exec_ioctl

// _rtld_exec_ioctl is the main interface from the runtime-loader/linker to the hosting
// WebAssembly virtual-machine, which requires to step into JavaScript land.

int _rtld_exec_ioctl(int cmd, void *argp) __WASM_IMPORT(rtld, exec_ioctl);

#define EXEC_IOCTL_COMPILE 557
#define EXEC_IOCTL_MKBUF 552
#define EXEC_IOCTL_WRBUF 553
#define EXEC_IOCTL_COREDUMP 580
#define EXEC_IOCTL_SET_START 581

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

// rtld_malloc.c
void *__crt_malloc(size_t nbytes);
void *__crt_calloc(size_t num, size_t size);
void __crt_free(void *cp);
extern int npagesizes;
extern size_t *pagesizes;
extern size_t page_size;

// rtld_mmap.c
#define	PROT_NONE	0x00	/* no permissions */
#define	PROT_READ	0x01	/* pages can be read */
#define	PROT_WRITE	0x02	/* pages can be written */
#define	PROT_EXEC	0x04	/* pages can be executed */

#define	MAP_SHARED	0x0001		/* share changes */
#define	MAP_PRIVATE	0x0002		/* changes are private */
#define	MAP_ANON	 0x1000	/* allocated from memory, swap space */

/**
 * Error return from mmap()
 */
#define MAP_FAILED	((void *)-1)

int __crt_mmap_init(void);
int __crt_munmap(void *, size_t);
void *__crt_mmap(void *, size_t, int, int, int, int64_t, int *);


#undef PAGE_SIZE
#undef PAGE_SHIFT
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12

#define WASM_PAGE_SIZE 65536

#endif /* __RTLD_WASM_INTERNAL_H_ */
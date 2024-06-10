


#ifndef __LIBWASM_KLD_IOCTL_H_
#define __LIBWASM_KLD_IOCTL_H_

#include <sys/stdint.h>

#include <stdbool.h>

#ifndef __WASM_IMPORT
#define __WASM_IMPORT(module, symbol) __attribute__((import_module(#module), import_name(#symbol)))
#endif

/*-
 * usage:
 *
 * kern.exec_ioctl(EXEC_IOCTL_MKBUF, struct wasm_loader_cmd_mkbuf *);
 */
int wasm_exec_ioctl(int cmd, void *arg) __WASM_IMPORT(kern, exec_ioctl);

#define EXEC_CTL_GET_USTKP 512
#define EXEC_CTL_SET_USTKP 513

#define EXEC_IOCTL_MKBUF 552
#define EXEC_IOCTL_WRBUF 553
#define EXEC_IOCTL_RDBUF 554
#define EXEC_IOCTL_CP_BUF_TO_MEM 555
#define EXEC_IOCTL_CP_KMEM_TO_UMEM 556
#define EXEC_IOCTL_COMPILE 557
#define EXEC_IOCTL_MAKE_UMEM 558
#define EXEC_IOCTL_UMEM_GROW 559
#define EXEC_IOCTL_RLOC_LEB 560
#define EXEC_IOCTL_RLOC_I32 561
#define EXEC_IOCTL_RUN_MODULE 562
#define EXEC_IOCTL_RUN_MODULE_AS_DYN_LD 563
#define EXEC_IOCTL_UTBL_MAKE 564
#define EXEC_IOCTL_UTBL_GROW 565
#define EXEC_IOCTL_DYNLD_DLSYM_EARLY 566
#define EXEC_IOCTL_BUF_REMAP 567
#define EXEC_IOCTL_RUN_RTLD_INIT 570
#define EXEC_IOCTL_RUN_RTLD_MAIN 571


struct wasm_loader_cmd_mkbuf {
    int32_t buffer;
    uint32_t size;
};

struct wasm_loader_cmd_compile {
    int32_t buffer;
    int32_t err;
    uint32_t strsz;
    char *strbuf;
};

/**
 * Used as `arg` for `EXEC_IOCTL_COMPILE`
 */
struct wasm_loader_cmd_compile_v2 {
    int32_t buffer;
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

struct wasm_loader_cmd_mk_umem {
    int32_t min;
    int32_t max;
    bool shared;
    uint8_t bits;   // 32 or 64 is valid values...
};

struct wasm_loader_cmd_umem_grow {
    int32_t grow_size;
    int32_t grow_ret; // old size or -1 
};

struct wasm_loader_cmd_wrbuf {
    int32_t buffer;
    void *src;
    uint32_t offset;
    uint32_t size;
};

struct wasm_loader_cmd_rdbuf {
    int32_t buffer;
    uint32_t src;
    void *dst;
    uint32_t size;
};

struct buf_remap_param {
    uint32_t dst;
    uint32_t src;
    uint32_t len;
};

struct wasm_loader_cmd_buf_remap {
    int32_t buffer;
    uint32_t new_size;
    uint32_t remap_count;
    struct buf_remap_param *remap_data;
};

struct wasm_loader_cmd_cp_buf_to_umem {
    int32_t buffer;
    uint32_t src_offset;
    uint32_t dst_offset;
    uint32_t size;
};

struct wasm_loader_cmd_cp_kmem_to_umem {
    int32_t buffer;
    void *src;
    uint32_t dst_offset;
    uint32_t size;
};

struct wasm_loader_cmd_rloc_leb {
    int32_t buffer;
    uint32_t count;
    uint32_t lebsz;
    void *packed_arr; // i32 + uleb|sleb (of lebsz)
};

struct wasm_loader_cmd_rloc_i32 {
    int32_t buffer;
    uint32_t count;     // number of i32 pairs in packed_arr
    void *packed_arr;   // i32 + i32
};

struct wasm_loader_cmd_run {
    int32_t buffer;
    int32_t flags;
    int32_t err;
    uint32_t strsz;
    char *strbuf;
};

struct wasm_loader_cmd_mk_table {
    int32_t min;
    int32_t max;
    uint8_t reftype;
    const char *module;
    const char *name;
};


#endif /* __LIBWASM_KLD_IOCTL_H_ */
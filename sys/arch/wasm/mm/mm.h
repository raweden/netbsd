

#ifndef _WASM_MM_PAGE_H_
#define _WASM_MM_PAGE_H_

#include "arch/wasm/include/bootinfo.h"
#include <stdint.h>
#include <sys/types.h>
#include <sys/rwlock.h>

#include <uvm/uvm_extern.h>
#include <uvm/uvm_pglist.h>

#include <wasm/bootinfo.h>

#ifndef WASM_PAGE_SIZE
#define WASM_PAGE_SIZE 65536
#endif

#define WASM_MEMORY_UNLIMITED -1

typedef unsigned int km_flag_t;

enum {
    WASM_MEM_AVAILABLE, // available
    WASM_MEM_BUSY,      // available but initialy marked as busy, 
    WASM_MEM_RESERVED   // never to be available
};

struct wasm_meminfo {
    struct btinfo_common common;
    uintptr_t addr_start;
    uintptr_t addr_end;
    unsigned usage;
};

struct wasm_bootstrap_info {
    int32_t chunksz;
    int32_t memory_max;    // maximum number of wasm-pages (65536 bytes) that can be obtained by memory.grow instruction.
    uint16_t meminfo_cnt;
    struct wasm_meminfo meminfo[];
};

/**
 * `struct vm_space_wasm` is the in kernel representation of a shared `WebAssembly.Memory` mapped by `mmblkd.js`
 */
struct vm_space_wasm {
    uint16_t memidx;
    uint8_t uspace;         // user-space or kernel-space or (user-space private)
    uint8_t ptrsz;          // 4 = 32-bit, 8 = 64-bit
    uint32_t refcount;
    uint32_t reserved;
    uint32_t maximum;
};

struct mmblkd_req {
    uint32_t op;
    uint32_t cv;
    uint32_t length;
    struct {
        uint32_t memidx;
        uint32_t addr;
    } src;
    struct {
        uint32_t memidx;
        uint32_t addr;
    } dst;
};

#define	PG_CLEAN	    0x00000001	/* page is known clean */
#define	PG_DIRTY	    0x00000002	/* page is known dirty */
#define	PG_BUSY		    0x00000004	/* page is locked */
#define	PG_PAGEOUT	    0x00000010	/* page to be freed for pagedaemon */
#define	PG_RELEASED	    0x00000020	/* page to be freed when unbusied */
#define	PG_FAKE		    0x00000040	/* page is not yet initialized */
#define	PG_RDONLY	    0x00000080	/* page must be mapped read-only */
#define	PG_TABLED	    0x00000200	/* page is tabled in object */
#define	PG_AOBJ		    0x00000400	/* page is part of an anonymous uvm_object */
#define	PG_ANON		    0x00000800	/* page is part of an anon, rather than an uvm_object */
#define	PG_FILE		    0x00001000	/* file backed (non-anonymous) */
#define	PG_READAHEAD    0x00002000	/* read-ahead but not "hit" yet */
#define	PG_FREE		    0x00004000	/* page is on free list */
#define PG_FREELIST     0x00008000  /* page is on free list (but only continious run of free pages is only one page) */
#define PG_FREETREE     0x00010000  /* page is in free tree  */

struct mm_page {
	/* _LP64: first cache line */
	union {
		TAILQ_ENTRY(mm_page) queue;	/* w: wired page queue or uvm_pglistalloc output */
        struct {
            struct mm_page *li_next;
            struct mm_page *li_prev;
        } list;
	} pageq;
	uint16_t		flags;		/* o: object flags */
    uint8_t         pgsz;       /* index into mm_pgsztbl */
	paddr_t			phys_addr;	/* o: physical address of pg */
	uint32_t		wire_count;	/* o,i: wired down map refs */
	struct vm_anon		*uanon;		/* o,i: anon */
	struct uvm_object	*uobject;	/* o,i: object */

	/* _LP64: second cache line */
	kmutex_t		interlock;	/* s: lock on identity */
};

void *kmem_alloc(size_t, km_flag_t);
void *kmem_zalloc(size_t, km_flag_t);
void kmem_free(void *, size_t);

struct mm_page *kmem_alloc_page(unsigned); // get one or more page and detach from free-list and increment ref count.
void kmem_free_page(struct mm_page *);

void kmem_get_page(struct mm_page *);
void kmem_put_page(struct mm_page *);

struct vm_space_wasm *new_uvmspace(void);
int ref_uvmspace(struct vm_space_wasm *);
int deref_uvmspace(struct vm_space_wasm *);


#endif
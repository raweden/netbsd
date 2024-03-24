

#ifndef _WASM_MM_PAGE_H_
#define _WASM_MM_PAGE_H_

#include "stdbool.h"
#include <sys/types.h>
#include <sys/rwlock.h>

#include <uvm/uvm_extern.h>
#include <uvm/uvm_pglist.h>
#include <uvm/uvm_page.h>

#include <wasm/bootinfo.h>

#ifndef WASM_PAGE_SIZE
#define WASM_PAGE_SIZE 65536
#endif

#ifndef UBC_WIN_PAGES_PREALLOC
#define UBC_WIN_PAGES_PREALLOC 1
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
#define PG_BYPASS_FILE_MAP   0x00020000  /* page bypasses file-mapping for IO, which does not alloc page(s) in ubc */

void *kmem_page_alloc(unsigned int, km_flag_t);
void *kmem_page_zalloc(unsigned int, km_flag_t);
void kmem_page_free(void *, uint32_t);
struct mm_page *paddr_to_page(void *);

struct mm_rangelist;

// TODO: use a uint8_t after pgsz which indicates the index of the bucket to which the page belongs.
struct mm_page {
	/* _LP64: first cache line */
	union {
		TAILQ_ENTRY(mm_page) queue;	/* w: wired page queue or uvm_pglistalloc output */
        struct {
            struct mm_page *li_next;
            struct mm_page *li_prev;
        } list;
        struct {
            struct mm_page *li_next;
            struct mm_rangelist *rb_tree;
        } tree;
	} pageq;
	uint32_t		flags;		/* o: object flags */
    uint8_t         pgsz;       /* index into mm_pgsztbl */
    uint8_t         bucket_index;   // TODO: implemenent buckets!
	paddr_t			phys_addr;	/* o: physical address of pg */
	uint32_t		wire_count;	/* o,i: wired down map refs */
	//struct vm_anon		*uanon;		/* o,i: anon */
	struct uvm_object	*uobject;	/* o,i: object */
    voff_t			offset;		/* o: offset into object */

	/* _LP64: second cache line */
	kmutex_t		interlock;	/* s: lock on identity */
};

struct mm_rangelist {
    struct mm_rangelist *next;
    struct mm_rangelist *prev;
    struct mm_page *first_page;
    void *bucket;
    uint32_t page_count;          // number of pages, that form a linear continious range of free memory
    uint16_t repeat;            // count of identical sized free segments (concatinates at start)
};

#ifndef RB_RED
#define RB_RED 1
#endif

#ifndef RB_BLACK
#define RB_BLACK 0
#endif

struct mm_page_rbnode {
    struct mm_page_rbnode *rb_parent;
    struct mm_page_rbnode *rb_lnode;
    struct mm_page_rbnode *rb_rnode;
    struct mm_page *pg_first;
    uint32_t pg_pages;  // number of pages, that form a linear continious range of free memory
    uint16_t pg_repeat;  // count of identical sized free segments (concatinates at start)
    uint8_t rb_color;
};


void *kmem_alloc(size_t, km_flag_t);
void *kmem_zalloc(size_t, km_flag_t);
void kmem_free(void *, size_t);

struct mm_page *kmem_alloc_page(unsigned); // get one or more page and detach from free-list and increment ref count.
void kmem_free_page(struct mm_page *);
void *kmem_first_in_list(void *);

void kmem_get_page(struct mm_page *);
void kmem_put_page(struct mm_page *);

struct vm_space_wasm *new_uvmspace(void);
int ref_uvmspace(struct vm_space_wasm *);
int deref_uvmspace(struct vm_space_wasm *);

// malloc

#define NSMALLBINS        (32U)
#define NTREEBINS         (32U)

typedef unsigned int bindex_t;
typedef unsigned int binmap_t;
typedef unsigned int flag_t; 

struct malloc_chunk {
  size_t               prev_foot;  /* Size of previous chunk (if free).  */
  size_t               head;       /* Size and inuse bits. */
  struct malloc_chunk* fd;         /* double links -- used only if free. */
  struct malloc_chunk* bk;
};

struct malloc_tree_chunk {
  /* The first four fields must be compatible with malloc_chunk */
  size_t                    prev_foot;
  size_t                    head;
  struct malloc_tree_chunk* fd;
  struct malloc_tree_chunk* bk;

  struct malloc_tree_chunk* child[2];
  struct malloc_tree_chunk* parent;
  bindex_t                  index;
};

struct malloc_segment {
  char*        base;             /* base address */
  size_t       size;             /* allocated size */
  struct malloc_segment* next;   /* ptr to next segment */
  flag_t       sflags;           /* mmap and extern flag */
};

struct malloc_state {
    binmap_t    smallmap;
    binmap_t    treemap;
    size_t      dvsize;
    size_t      topsize;
    char*       least_addr;
    struct malloc_chunk *dv;
    struct malloc_chunk *top;
    size_t      trim_check;
    size_t      release_checks;
    size_t      magic;
    struct malloc_chunk *smallbins[(NSMALLBINS+1)*2];
    struct malloc_tree_chunk *treebins[NTREEBINS];
    size_t      footprint;
    size_t      max_footprint;
    size_t      footprint_limit; /* zero means no limit */
    flag_t      mflags;
    kmutex_t    mutex;  /* locate lock among fields that rarely change */
    struct malloc_segment seg;
    void*       extp;      /* Unused but available for extensions */
    size_t      exts;
};

#ifndef VMEM_NAME_MAX
#define VMEM_NAME_MAX 16
#endif

struct mm_arena {
    struct malloc_state mstate;
    kmutex_t malloc_lock;
    struct mm_page *first_page;
    struct mm_page *last_page;
    uint32_t page_count;
    char mm_name[VMEM_NAME_MAX + 1];
};

struct mm_arena *mm_arena_init(struct mm_arena *vm, const char *name,
    vmem_addr_t base, vmem_size_t size, vmem_size_t quantum,
    vmem_import_t *importfn, vmem_release_t *releasefn,
    vmem_t *arg, vmem_size_t qcache_max, vm_flag_t flags, int ipl);

struct mm_arena *mm_arena_create(const char *name, vmem_addr_t base, vmem_size_t size,
    vmem_size_t quantum, vmem_import_t *importfn, vmem_release_t *releasefn,
    vmem_t *source, vmem_size_t qcache_max, vm_flag_t flags, int ipl);

struct mm_arena *mm_arena_xcreate(const char *, vmem_addr_t, vmem_size_t, 
    vmem_size_t, vmem_ximport_t *, vmem_release_t *, vmem_t *, vmem_size_t, 
    vm_flag_t, int);

int mm_arena_alloc(struct mm_arena *vm, vmem_size_t size, vm_flag_t flags, vmem_addr_t *addrp);

int mm_arena_xalloc(struct mm_arena *vm, const vmem_size_t size0, vmem_size_t align,
    const vmem_size_t phase, const vmem_size_t nocross,
    const vmem_addr_t minaddr, const vmem_addr_t maxaddr, const vm_flag_t flags,
    vmem_addr_t *addrp);

void mm_arena_free(struct mm_arena *vm, vmem_addr_t addr, vmem_size_t size);
void mm_arena_destroy(struct mm_arena *vm);

extern struct vm_space_wasm *__wasm_kmeminfo;

static inline bool
vm_space_is_kernel(struct vm_space_wasm *space)
{
    return space == __wasm_kmeminfo;
}


struct mm_arena *mm_arena_create_simple(const char *name, int *error);
void *mm_arena_alloc_simple(struct mm_arena *vm, size_t size, int *error);
void *mm_arena_zalloc_simple(struct mm_arena *vm, size_t size, int *error);
void mm_arena_free_simple(struct mm_arena *vm, void *addr);
void mm_arena_destroy_simple(struct mm_arena *vm);

#endif
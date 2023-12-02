
#include <sys/types.h>
#include <sys/mutex.h>
#include <sys/null.h>
#include <sys/errno.h>
#include <math.h>

#include <wasm/wasm_inst.h>
#include <wasm/wasm_module.h>
#include <wasm/bootinfo.h>

#include "mm.h"

#ifndef SUPER_PAGE_SIZE
#define SUPER_PAGE_SIZE 4194304
#endif

uint32_t mm_pgsztbl[3] = {
    4194304,
    65536,
    4096,
};

struct kmem_data_private {
    unsigned unmapped_raw;      // no of bytes that is unmapped (reserved outside of allocaion example init memory) excluding padding to nearest page boundary,
    unsigned unmapped_pad;      // same as above but includes the padding for nearest page boundary,
    unsigned rsvd_pgs;          // reserved/unmapped memory in number of pages.
    unsigned pages_total;       // number of mapped pages busy and free.
    unsigned pages_busy;
    unsigned pages_pool;    // pages for pool allocation (also accounted in pages_busy)
    unsigned pages_malloc;  // number of pages (also accounted in pages_busy) that is used for allocation by kmem_malloc()
    unsigned malloc_busy;
    unsigned malloc_free;
} __kmem_data;

struct mmblkd_event {
    uint32_t event; 
};

struct kmem_mmblkd_head {
    uint32_t hstate;
    struct mmblkd_listeners {
        unsigned int li_flags;
        int32_t li_seq;
        void *li_data;
        void (*li_callback)(void *, void *); // arg0 = li_data arg1 = event
    } clients[2];
    struct mmblkd_req *req_queue[32];
    struct mmblkd_event *evt_queue[32];
} __mmblkd_head;

bool wasm_kmem_growable;
int32_t wasm_kmem_avail;
int32_t wasm_kmem_limit;
static kmutex_t __malloc_mtx;
static kmutex_t __kgrow_lock;

struct mm_page *pg_avail;
struct mm_page *pg_unmapped; // unused page object in table, not yet mapped. first page should be mapped lowest part of next non-reserved grow.
struct mm_page *pg_metatbl;
struct mm_page **pg_avec;     // pg_avec[idx in smallest pgsz] mapped to struct mm_page at that addr.
uint32_t pg_avecsz;
struct pool *pg_metapool; // alternative to pg_metatbl?

struct rb_tree *pg_ftree;   // organizing all free pages in a tree based on continious run of free-pages.
struct mm_page *pg_sfree;   // pageq.list of single free page(s) meaning that the phys page before and after are not free

// TODO: this should be dynamic..
static kmutex_t __uvmspaces_lock;
unsigned int __uvmspaces_bmap[2];
struct vm_space_wasm __uvmspaces[64];
struct vm_space_wasm *__wasm_kmeminfo;

// does this link to correct stack-pointer? Nope!
extern void *__stack_pointer;

#ifdef __WASM
#define __WASM_BUILTIN(symbol) __attribute__((import_module("__builtin"), import_name(#symbol)))
#else
__WASM_BUILTIN(x)
#endif

/**
 * translates to `memory.size` instruction in post-edit (or link-time with ylinker)
 * @return The size of memory container in units of wasm pages (65536 bytes)
 */
int wasm_memory_size(void) __WASM_BUILTIN(memory_size);

/**
 * translates to `memory.grow` instruction in post-edit (or link-time with ylinker)
 *
 * @param pgs Number of wasm pages (65536 bytes) to grow memory container by.
 * @return The size before memory was grown or -1 if memory cannot be allocated.
 */
int wasm_memory_grow(int pgs) __WASM_BUILTIN(memory_grow);

/**
 * translates to `memory.fill` instruction in post-edit (or link-time with ylinker)
 *
 * @param dst The destination address.
 * @param val The value to use as fill
 * @param len The number of bytes to fill.
 */
void wasm_memory_fill(void * dst, int32_t val, uint32_t len) __WASM_BUILTIN(memory_fill);

// arg0 = mem-info, arg1 = bt-info
void __wasm_bootstrap_info(void *, void *) __WASM_IMPORT(kern, __bootstrap_info);
void __wasm_kexec_ioctl(int, void *) __WASM_IMPORT(kern, exec_ioctl);

#ifndef WASM_MAX_PHYS_SEG
#define WASM_MAX_PHYS_SEG 32
#endif

#ifndef atop
#define atop(x)     ((unsigned long)(x) >> PAGE_SHIFT)
#endif
#ifndef ptoa
#define ptoa(x)     ((unsigned long)(x) << PAGE_SHIFT)
#endif

struct mm_phys_seg {
    uintptr_t start;
    uintptr_t end;
    uintptr_t pg_start;
    uintptr_t pg_end;
    uint32_t pg_cnt;
    //void *owner;
    struct mm_phys_seg *next;
    struct mm_page *first;
    struct mm_page *last;
    uint8_t type;
    uint8_t pgsz;
    uint16_t pgflags;
    uint8_t pgbacked;
};

static void
phys_seg_split_head(struct mm_phys_seg *before, struct mm_phys_seg *src, struct mm_phys_seg *dst)
{

}

static void
phys_seg_split_tail(struct mm_phys_seg *before, struct mm_phys_seg *src, struct mm_phys_seg *dst)
{
    
}

static void
pg_detach_list(struct mm_page *first, struct mm_page *last)
{
    struct mm_page *before, *after;
    before = first->pageq.list.li_prev;
    after = last->pageq.list.li_next;
    before->pageq.list.li_next = after;
    after->pageq.list.li_prev = before;
    first->pageq.list.li_prev = NULL;
    last->pageq.list.li_next = NULL;
}

void
init_wasm_memory(void)
{
    struct wasm_bootstrap_info *info;
    void *tstackp;
    uint32_t pgcnt, pgsrnd, pgdatasz, pgmetapgs, pgarrsz, pg_arr_pgs, rsvd_raw, rsvd_pad, rsvd_pgs, bucket_cnt;
    int32_t rem;
    int32_t wapgs = wasm_memory_size();
    int32_t avail = wapgs * WASM_PAGE_SIZE;
    struct btinfo_memmap *bt_mmap = lookup_bootinfo(BTINFO_MEMMAP);
    uintptr_t metaptr;  // meta table start addr.
    uintptr_t pvecptr;  // page-vector table start addr.
    struct mm_phys_seg *metaseg;    // index of phys-seg that fit meta table.
    struct mm_phys_seg *pvecseg;    // index of phys-seg that fit page-array table.
    struct mm_phys_seg *iomemseg;

    wasm_memory_fill(&buckets[0], 0x00, sizeof(buckets));

    pgrb_bucket->pgcnt = 6; // 1024 mm_page_rbnode items


    mutex_init(&__malloc_mtx, MUTEX_SPIN, 0);
    mutex_init(&__uvmspaces_lock, MUTEX_SPIN, 0);

    __wasm_kmeminfo = &__uvmspaces[0];
    __uvmspaces_bmap[0] = (1 << 0);

    void *argp[2];
    argp[0] = &__mmblkd_head;
    argp[1] = __wasm_kmeminfo;
    __wasm_kexec_ioctl(552, argp);

    if (bt_mmap->memory_max == WASM_MEMORY_UNLIMITED) {
        wasm_kmem_limit = WASM_MEMORY_UNLIMITED;
    } else {
        wasm_kmem_limit = bt_mmap->memory_max * WASM_PAGE_SIZE;
    }

    wasm_kmem_growable = (avail != wasm_kmem_limit);

    __wasm_kmeminfo->uspace = 1;
    __wasm_kmeminfo->refcount = 1;
    __wasm_kmeminfo->ptrsz = sizeof(void *);
    __wasm_kmeminfo->reserved = avail;
    __wasm_kmeminfo->maximum = wasm_kmem_limit;

    // compute the number of pages we need
    // when kernel memory is growable, allocate in optimal ranges or to cover limit.
    pgcnt = 0;

    struct mm_phys_seg segs[WASM_MAX_PHYS_SEG];
    struct mm_phys_seg *firstseg, *seg, *lastseg, *nseg;
    int segcnt, si = 0;

    memset(&segs, 0, sizeof(struct mm_phys_seg) * WASM_MAX_PHYS_SEG);

    int entcnt = bt_mmap->num;
    lastseg = NULL;
    struct bi_memmap_entry *ent = bt_mmap->entry;
    for (int i = 0; i < entcnt; i++) {
        uintptr_t rem, end, pg_start, pg_end, start = ent->addr;
        end = start + ent->size;
        // align down
        rem = start != 0 ? start % PAGE_SIZE : 0;
        pg_start = rem != 0 ? (start - rem) : start;

        // align up
        rem = end != 0 ? end % PAGE_SIZE : 0;
        pg_end = rem != 0 ? (end + (PAGE_SIZE - rem)) : end;

        if ((lastseg == NULL && pg_start != 0) || lastseg->pg_end != pg_start) {
            seg = &segs[si++];
            seg->type = BIM_Memory;
            seg->pgbacked = 1;
            seg->end = start;
            seg->pg_end = pg_start;
            if (lastseg == NULL) { 
                seg->start = 0;
                seg->pg_start = 0;
                seg->pg_cnt = pg_start != 0 ? pg_start / PAGE_SIZE : 0;
            } else {
                seg->start = lastseg->end;
                seg->pg_start = lastseg->pg_end;
                seg->pg_cnt = (seg->pg_start != seg->pg_end) ? (seg->pg_end - seg->pg_start) / PAGE_SIZE : 0;
            }

            if (firstseg == NULL)
                firstseg = seg;
            if (lastseg != NULL)
                lastseg->next = seg;
            
            lastseg = seg;
        }
        seg = &segs[si++];
        if (firstseg == NULL)
            firstseg = seg;
        if (lastseg != NULL)
            lastseg->next = seg;

        if (ent->type == BIM_Memory || ent->type == BIM_IOMEM) {
            seg->pgbacked = 1;
        }
        
        seg->start = start;
        seg->end = end;
        seg->type = ent->type;
        seg->pg_start = pg_start;
        seg->pg_end = pg_end;
        seg->pg_cnt = (pg_start != pg_end) ? (pg_end - pg_start) / PAGE_SIZE : 0;
        lastseg = seg;
        ent++;
    }

    if (lastseg->pg_end < avail && lastseg->end < avail) {
        seg = &segs[si++];
        seg->start = lastseg->end;
        seg->pg_start = lastseg->pg_end;
        seg->end = avail;
        lastseg->next = seg;

        // align down
        uintptr_t rem = avail != 0 ? avail % PAGE_SIZE : 0;
        seg->pg_end = rem != 0 ? (avail - rem) : avail;
    }
    rsvd_raw = 0;
    rsvd_pad = 0;
    seg = firstseg;
    for (int i = 0; i < si; i++) {
        printf("{type %d start %lu end %lu pg_start %lu pg_end %lu pg_cnt %u }", seg->type, seg->start, seg->end, seg->pg_start, seg->pg_end, seg->pg_cnt);
        if (seg->type == BIM_Reserved) {
            rsvd_raw += (seg->end - seg->start);
            rsvd_pad += (seg->pg_end - seg->pg_start);
            rsvd_pgs += seg->pg_cnt;
            seg = seg->next;
            continue;
        } else if (seg->pgbacked != TRUE) {
            seg = seg->next;
            continue;
        }
        pgcnt += seg->pg_cnt;
        seg = seg->next;
    }

    pgdatasz = sizeof(struct mm_page) * pgcnt;
    pgmetapgs = (pgdatasz / PAGE_SIZE) + ((pgdatasz % PAGE_SIZE) != 0 ? 1 : 0); // fast-ceil
    pgsrnd = (pgmetapgs * PAGE_SIZE) / sizeof(struct mm_page);
    pgarrsz = sizeof(void *) * (avail / PAGE_SIZE);
    rem = pgarrsz % PAGE_SIZE;
    if (rem != 0) {
        pgarrsz += (PAGE_SIZE - rem);
    }
    pg_arr_pgs = (pgarrsz / PAGE_SIZE);


    printf("should alloc %d mm pages, data-size: %d (%d pages) rounded cnt: %d page-array-size: %d page-array-pages: %d\n", pgcnt, pgdatasz, pgmetapgs, pgsrnd, pgarrsz, pg_arr_pgs);

    lastseg = NULL;
    metaseg = NULL;
    pvecseg = NULL;
    iomemseg = NULL;

    seg = firstseg;
    for (int i = 0; i < si; i++) {

        if (iomemseg == NULL && seg->type == BIM_IOMEM) {
            iomemseg = seg;
        }

        if (seg->type != BIM_Memory) {
            lastseg = seg;
            seg = seg->next;
            continue;
        }

        if (seg->pg_cnt >= pgmetapgs) {

            if (seg->pg_cnt == pgmetapgs) {
                // consume segment
                bucket->addr = seg->pg_start;
                metaseg = seg;
            } else {
                // split segment
                nseg = &segs[si++];
                nseg->type = BIM_Memory;
                nseg->pgbacked = 1;
                nseg->pgflags = PG_BUSY;
                nseg->next = seg;
                lastseg->next = nseg;
                nseg->pg_start = seg->pg_start;
                nseg->start = nseg->pg_start;
                nseg->pg_cnt = pgmetapgs;
                nseg->pg_end = nseg->pg_start + (pgmetapgs * PAGE_SIZE);
                nseg->end = nseg->pg_end;
                seg->pg_start = nseg->pg_end;
                seg->start = seg->pg_start;
                seg->pg_cnt -= pgmetapgs;
                metaptr = nseg->pg_start;
                metaseg = nseg;
                lastseg = nseg;
                continue;
            }
        }
        
        if (metaseg == NULL && seg->pg_cnt >= pgmetapgs) {

            
        }
        
        if (pvecseg == NULL && seg->pg_cnt >= pg_arr_pgs) {
            
            if (seg->pg_cnt == pg_arr_pgs) {
                // consume segment
                pvecptr = seg->pg_start;
                pvecseg = seg;
            } else {
                // split
                nseg = &segs[si++];
                nseg->type = BIM_Memory;
                nseg->pgbacked = 1;
                nseg->pgflags = PG_BUSY;
                nseg->next = seg;
                lastseg->next = nseg;
                nseg->pg_start = seg->pg_start;
                nseg->start = nseg->pg_start;
                nseg->pg_cnt = pg_arr_pgs;
                nseg->pg_end = nseg->pg_start + (pg_arr_pgs * PAGE_SIZE);
                nseg->end = nseg->pg_end;
                seg->pg_start = nseg->pg_end;
                seg->start = seg->pg_start;
                seg->pg_cnt -= pg_arr_pgs;
                pvecptr = nseg->pg_start;
                pvecseg = nseg;
                lastseg = nseg;
                continue;
            }
        }
        
        lastseg = seg;
        seg = seg->next;
    }

    // DEBUG PRINT
    printf("before pg init\n");
    seg = firstseg;
    for (int i = 0; i < si; i++) {
        printf("{type %d page-backed: %s start %lu pg_start %lu end %lu pg_end %lu pg_cnt %u next: %p}", seg->type, seg->pgbacked ? "YES" : "NO", seg->start, seg->pg_start, seg->end, seg->pg_end, seg->pg_cnt, seg->next);
        seg = seg->next;
    }

    struct mm_page *first, *prev, *pg;
    pg = (struct mm_page *)metaptr;
    first = pg;
    prev = NULL;

    printf("metaptr = %p pgvec-ptr %p\n", first, (void *)pvecptr);

    for (int i = 0; i < pgsrnd; i++) {
        pg->pgsz = 2;
        pg->flags = 0;
        pg->pageq.list.li_prev = prev;
        if (prev)
            prev->pageq.list.li_next = pg;
        prev = pg;
        pg++;
    }

    printf("after pg init\n");
    seg = firstseg;
    for (int i = 0; i < si; i++) {
        printf("{type %d page-backed: %s start %lu pg_start %lu end %lu pg_end %lu pg_cnt %u next: %p}", seg->type, seg->pgbacked ? "YES" : "NO", seg->start, seg->pg_start, seg->end, seg->pg_end, seg->pg_cnt, seg->next);
        seg = seg->next;
    }

    struct mm_page **pgavec = (struct mm_page **)pvecptr;
    pg = first;
    seg = firstseg;
    for (int x = 0; x < si; x++) {

        if (seg->pgbacked != TRUE) {
            seg = seg->next;
            continue;
        }

        struct mm_page **bucket_pgvec = pgavec + (seg->pg_start / PAGE_SIZE);
        struct mm_page *firstpg, *lastpg;
        uintptr_t phys_addr = seg->pg_start;
        uint32_t segpgsz = PAGE_SIZE;
        uint16_t pgflags;
        int ylen = seg->pg_cnt;

        if (seg->pgflags) {
            pgflags = seg->pgflags;
        } else {
            pgflags = PG_FREE;
        }

        firstpg = pg;
        for (int y = 0; y < ylen; y++) {
            pg->phys_addr = phys_addr;
            pg->flags = pgflags;
            phys_addr += segpgsz;
            *bucket_pgvec = pg;
            lastpg = pg;
            bucket_pgvec++;
            pg++;
        }

        seg->first = firstpg;
        seg->last = lastpg;

        seg = seg->next;
    }

    pg_avec = pgavec;
    pg_avecsz = pgarrsz / sizeof(void *);

    // disconnect busy pages
    pg_detach_list(metaseg->first, metaseg->last);
    pg_metatbl = metaseg->first;

    pg_detach_list(pvecseg->first, pvecseg->last);
    if (iomemseg)
        pg_detach_list(iomemseg->first, iomemseg->last);

    uint32_t pages_busy = 0;
    pages_busy += pgmetapgs ;
    pages_busy += pg_arr_pgs;

    atomic_store32(&__kmem_data.unmapped_raw, rsvd_raw);
    atomic_store32(&__kmem_data.unmapped_pad, rsvd_pad);
    atomic_store32(&__kmem_data.rsvd_pgs, rsvd_pgs);
    atomic_store32(&__kmem_data.pages_total, pgcnt);
    atomic_store32(&__kmem_data.pages_busy, pages_busy);
}

void grow_kernel_memory(unsigned int wapgs)
{
    mutex_enter(&__kgrow_lock);

    mutex_exit(&__kgrow_lock);
}

struct mm_page *
paddr_to_page(void *addr)
{
    uintptr_t idx = atop(addr);
    if (idx >= pg_avecsz) {
        return NULL;
    }
    return pg_avec[idx];
}

void
kmem_allocpgs(unsigned int pgs, km_flag_t flags)
{
    // TODO: use rb tree to organize pages by continuous range?



    atomic_sub32(&__kmem_data.pages_busy, pgs);
}

void
kmem_freepgs(struct mm_page **pgs, unsigned int cnt)
{
    uint32_t pgsz, continuous, p, idx = 0;
    uintptr_t pa;
    uintptr_t lastend;
    struct mm_page *past, *last, *pg = pgs[idx++];
    last = NULL;
    continuous = TRUE;

    while (idx < cnt) {
        pa = pg->phys_addr;
        pg->flags &= ~(PG_BUSY);
        struct mm_page *lastc = NULL;
        if (last) {

            if (last->pgsz != pg->pgsz || lastend != pa) {
                continuous = FALSE;
            } else {
                lastc = last;
            }
        }

        if (!lastc) {
            p = atop(pg->phys_addr);
            past = pa > 0 ? pg_avec[p - 1] : NULL;
            if (past && (past->flags & PG_FREE) != 0) {
                
            }
        }

        lastend = pg->phys_addr + mm_pgsztbl[pg->pgsz];
        last = pg;
        pg = pgs[idx++];
    }

    atomic_add32(&__kmem_data.pages_busy, cnt);
}

void
kmem_free_pgsrange(struct mm_page *first, struct mm_page *last)
{

}

struct vm_space_wasm *
new_uvmspace(void)
{
    struct vm_space_wasm *space;
    unsigned int bmap;
    int index;
    // finding first free in bitmap
    printf("%s new uvmspace = %p\n", __func__, space);
    mutex_enter(&__uvmspaces_lock);

    index = -1;
    for (int y = 0; y < 2; y++) {
        bmap = __uvmspaces_bmap[y];
        for (int shift = 0; shift < 32; shift++) {
            unsigned int bit = (1 << shift);
            if ((bmap & bit) == 0) {
                index = (y * 32) + shift;
                __uvmspaces_bmap[y] |= bit;
                break;
            }
        }
    }

    printf("%s new uvmspace at index %d\n", __func__, index);

    mutex_exit(&__uvmspaces_lock);

    return index != -1 ? &__uvmspaces[index] : NULL;
}

static void
free_uvmspace(struct vm_space_wasm *space)
{
    unsigned int bmap;
    int index;

    printf("%s freeing uvmspace = %p\n", __func__, space);

    mutex_enter(&__uvmspaces_lock);

    index = -1;
    for (int y = 0; y < 64; y++) {
        struct vm_space_wasm *other = &__uvmspaces[y];
        if (space == other) {
            index = y;
            break;
        }
    }

    printf("%s found uvmspace at index %d\n", __func__, index);

    if (index != -1) {
        int y = index != 0 ? index / 32 : 0;
        int x = index != 0 ? index % 32 : 0;

        __uvmspaces_bmap[y] &= ~(1 << x);
    }

    mutex_enter(&__uvmspaces_lock);
}

int
ref_uvmspace(struct vm_space_wasm *space)
{
    uint32_t res, old = atomic_load32(&space->refcount);
    if (old == 0) {
        printf("%s uvmspace = %p refcount already 0 (zero)\n", __func__, space);
        return EINVAL;
    }

    res = atomic_cmpxchg32(&space->refcount, old, old + 1);
    if (res == old) {
        return 0;
    }

    printf("%s uvmspace = %p could not update refcount\n", __func__, space);
    return EINVAL;
}

int deref_uvmspace(struct vm_space_wasm *space)
{
    uint32_t res, nv, old = atomic_load32(&space->refcount);
    if (old == 0) {
        printf("%s uvmspace = %p refcount already 0 (zero)\n", __func__, space);
        return EINVAL;
    }

    nv = old - 1;
    res = atomic_cmpxchg32(&space->refcount, old, nv);
    if (res == old) {
        if (nv == 0) {
            free_uvmspace(space);
        }
        return 0;
    }

    printf("%s uvmspace = %p could not update refcount\n", __func__, space);
    return EINVAL;
}

#if 0
void *
kmem_alloc(size_t size, unsigned int flags)
{
    // aquire lock
    mutex_enter(&__malloc_mtx);

    atomic_add32(&__kmem_data.malloc_busy, size);
    mutex_exit(&__malloc_mtx);
}

void *
kmem_zalloc(size_t size, km_flag_t kmflags)
{

}

void
kmem_free(void *ptr, size_t size)
{
    mutex_enter(&__malloc_mtx);

    atomic_sub32(&__kmem_data.malloc_busy, size);
    mutex_exit(&__malloc_mtx);
}
#endif
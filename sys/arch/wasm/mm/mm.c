
#include <sys/types.h>
#include <sys/mutex.h>
#include <sys/null.h>
#include <sys/errno.h>
#include <sys/pool.h>
#include <sys/tree.h>
#include <math.h>

#include <wasm/wasm_inst.h>
#include <wasm/wasm_module.h>
#include <wasm/bootinfo.h>
#include <dev/isa/isareg.h>

#include <machine/wasm-extra.h>

#include "arch/wasm/include/cpu.h"
#include "arch/wasm/include/types.h"
#include "arch/wasm/include/vmparam.h"
#include "arch/wasm/include/wasm-extra.h"
#include "arch/wasm/include/wasm_inst.h"
#include "mm.h"
#include "param.h"
#include "queue.h"
#include "stdbool.h"

#ifndef SUPER_PAGE_SIZE
#define SUPER_PAGE_SIZE 4194304
#endif

#ifndef MEMORY_DEBUG
#define MEMORY_DEBUG 1
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

size_t nkmempages;

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

// from subr_pool.c
typedef uint32_t pool_item_bitmap_t;
struct pool_item_header {
	/* Page headers */
	LIST_ENTRY(pool_item_header)
				ph_pagelist;	/* pool page list */
	union {
		/* !PR_PHINPAGE */
		struct {
			SPLAY_ENTRY(pool_item_header)
				phu_node;	/* off-page page headers */
		} phu_offpage;
		/* PR_PHINPAGE */
		struct {
			unsigned int phu_poolid;
		} phu_onpage;
	} ph_u1;
	void *			ph_page;	/* this page's address */
	uint32_t		ph_time;	/* last referenced */
	uint16_t		ph_nmissing;	/* # of chunks in use */
	uint16_t		ph_off;		/* start offset in page */
	union {
		/* !PR_USEBMAP */
		struct {
			LIST_HEAD(, pool_item)
				phu_itemlist;	/* chunk list for this page */
		} phu_normal;
		/* PR_USEBMAP */
		struct {
			pool_item_bitmap_t phu_bitmap[1];
		} phu_notouch;
	} ph_u2;
};

// TODO: unify this into something similar to uvmexp (struct uvmexp)
// variables.
extern vaddr_t msgbuf_vaddr;
extern unsigned int msgbuf_p_cnt;
bool wasm_kmem_growable;
int32_t wasm_kmem_avail;
int32_t wasm_kmem_limit;
static kmutex_t __malloc_mtx;
static kmutex_t __kgrow_lock;
paddr_t avail_end;
psize_t physmem;

struct mm_page *pg_avail;
struct mm_page *pg_unmapped; // unused page object in table, not yet mapped. first page should be mapped lowest part of next non-reserved grow.
struct mm_page *pg_metatbl;
struct mm_page **pg_avec;     // pg_avec[idx in smallest pgsz] mapped to struct mm_page at that addr.
uint32_t pg_avecsz;
struct pool *pg_metapool; // alternative to pg_metatbl?

#if MEMORY_DEBUG
struct mm_page *pg_first_addr;
struct mm_page *pg_last_addr;
#endif

static struct mm_pg_range_list {
    kmutex_t pg_rangelist_lock;          // lock to move/alloc/free pages in range
    struct mm_rangelist *max;
    struct mm_rangelist *min;
} pg_rangelist;

static struct pool rb_node_pool;
static char rb_pool_items_buf[336];
static struct mm_page *rl_pages;

struct mm_page *dcon_pg_list;       // pageq.list of single free page(s) meaning that the phys page before and after are not free
static uint32_t dcon_pg_cnt;        // number of non-continious pages (free page ranges which are just a single page)
static kmutex_t dcon_pg_lock;

// TODO: this should be dynamic..
static kmutex_t __uvmspaces_lock;
unsigned int __uvmspaces_bmap[2];
struct vm_space_wasm __uvmspaces[64];
struct vm_space_wasm *__wasm_kmeminfo;
static struct pool mm_space_pool;

#ifdef __WASM
#define __WASM_BUILTIN(symbol) __attribute__((import_module("__builtin"), import_name(#symbol)))
#else
__WASM_BUILTIN(x)
#endif

void mm_dump_rangelist(void);

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


static void *rangelist_pool_alloc(struct pool *, int);
static void rangelist_pool_free(struct pool *, void *);

static struct pool_allocator pool_allocator_rangelist = {
    .pa_alloc = rangelist_pool_alloc,
    .pa_free = rangelist_pool_free,
    .pa_pagesz = PAGE_SIZE,
};

static void *mm_space_pool_alloc(struct pool *, int);
static void mm_space_pool_free(struct pool *, void *);

static struct pool_allocator pool_allocator_mm_space = {
    .pa_alloc = mm_space_pool_alloc,
    .pa_free = mm_space_pool_free,
    .pa_pagesz = PAGE_SIZE,
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

struct mm_page *paddr_to_page(void *addr);

#if MEMORY_DEBUG
static __noinline bool
mm_sanity_check_is_valid_page(struct mm_page *pg, bool dump)
{
    if (pg < pg_first_addr || pg > pg_last_addr) {
        if (dump)
            printf("%s ERROR invalid page %p provided\n", __func__, pg);
        return false;
    }

    return true;
}

static __noinline void
mm_assert_page_vector(void)
{
    uintptr_t addr = 0;
    uint32_t count = pg_avecsz;
    for (int i = 0; i < count; i++) {
        struct mm_page *pg = pg_avec[i];
        if (pg != NULL) {
            if (!mm_sanity_check_is_valid_page(pg, false)) {
                printf("%s ERROR found invalid page at index = %d page-addr = %p", __func__, i, pg);
            } else if (pg->phys_addr != addr) {
                printf("%s ERROR page at index = %d page-addr = %p does not have the expected %p vs. actual = %p", __func__, i, pg, (void *)addr, (void *)pg->phys_addr);
            }
        }
        addr += PAGE_SIZE;
    }
}

static __noinline void
mm_sanity_check_tree_nodes(void)
{
    struct mm_rangelist *node, *last;
    bool pass = true;

    node = pg_rangelist.max;
    last = NULL;

    while (node != NULL) {

        if (node->next != last) {
            printf("%s ERROR found node %p where bi-directial linking is broken (actual = %p expected = %p)", __func__, node, node->next, last);
            pass = false;
        }

        if (node->first_page == NULL || mm_sanity_check_is_valid_page(node->first_page, false) == false) {
            printf("%s ERROR found node %p with invalid first page %p", __func__, node, node->first_page);
            pass = false;
        }

        last = node;
        node = node->prev;
    }

    if (!pass) {
        mm_dump_rangelist();
    }
}

static __noinline void
mm_sanity_check_tree_nodes_before(void)
{
    struct mm_rangelist *node, *last;
    bool pass = true;

    node = pg_rangelist.max;
    last = NULL;

    while (node != NULL) {

        if (node->next != last) {
            printf("%s ERROR found node %p where bi-directial linking is broken (actual = %p expected = %p)", __func__, node, node->next, last);
            pass = false;
        }

        if (node->first_page == NULL || mm_sanity_check_is_valid_page(node->first_page, false) == false) {
            printf("%s ERROR found node %p with invalid first page %p", __func__, node, node->first_page);
            pass = false;
        }

        last = node;
        node = node->prev;
    }

    if (!pass) {
        mm_dump_rangelist();
    }
}

static __noinline void
mm_sanity_check_tree_nodes_after(void)
{
    struct mm_rangelist *node, *last;
    bool pass = true;

    node = pg_rangelist.max;
    last = NULL;

    while (node != NULL) {

        if (node->next != last) {
            printf("%s ERROR found node %p where bi-directial linking is broken (actual = %p expected = %p)", __func__, node, node->next, last);
            pass = false;
        }

        if (node->first_page == NULL || mm_sanity_check_is_valid_page(node->first_page, false) == false) {
            printf("%s ERROR found node %p with invalid first page %p", __func__, node, node->first_page);
            pass = false;
        }

        last = node;
        node = node->prev;
    }

    if (!pass) {
        mm_dump_rangelist();
    }
}

#endif

static inline void
mm_rangelist_detatch(struct mm_pg_range_list *root, struct mm_rangelist *node)
{
    if (node == root->min) {
        root->min = node->next;
    } else if (node == root->max) {
        root->max = node->prev;
    }

    if (node->prev != NULL)
        node->prev->next = node->next;
    if (node->next != NULL)
        node->next->prev = node->prev;
}

/**
 * @return The last `struct mm_page` in the range, without detatching .next
 */
static struct mm_page *
mm_move_page_range(struct mm_page *first, struct mm_rangelist *rlist, uint32_t count)
{
    if (first == NULL) {
        printf("%s ERROR invoked with NULL as first page count = %d", __func__, count);
        mm_dump_rangelist();
        return NULL;
    }

    struct mm_page *last, *pg = first;

    for (int i = 0; i < count; i++) {
        pg->pageq.tree.rb_tree = rlist;
        last = pg;
        pg = pg->pageq.tree.li_next;
    }

    return last;
}

static struct mm_rangelist *
mm_rangelist_ge_topdown(uint32_t count)
{
    struct mm_rangelist *node = pg_rangelist.max;
    struct mm_rangelist *last;

    while (node != NULL) {
        
        if (node->page_count < count) {
            return last;
        }

        last = node;
        node = node->prev;
    }

    return NULL;
}

static struct mm_rangelist *
mm_rangelist_ge_bottomup(uint32_t count)
{
    struct mm_rangelist *node = pg_rangelist.min;

    while (node != NULL) {
        
        if (node->page_count < count) {
            node = node->next;
            continue;
        }

        return node;
    }

    return NULL;
}

/**
 * Returns the node before which a node of count should be inserted. Returns `NULL` if node is already less than the count.
 */
static struct mm_rangelist *
mm_rangelist_ge_below(struct mm_rangelist *node, uint32_t count)
{
    struct mm_rangelist *last = node;
    node = node->prev;

    if (last->page_count < count) {
        return NULL;
    }

    while (node != NULL) {
        
        if (node->page_count < count) {
            return last;
        }

        last = node;
        node = node->prev;
    }

    return last;
}

// sorted from 0-9 (where ->next is greater and ->prev is smaller)
static void
mm_put_page_range(struct mm_pg_range_list *root, struct mm_page *first, struct mm_page *last, uint32_t pgcnt)
{
    if (first == NULL) {
        printf("%s ERROR invoked with NULL as first page - count = %d\n", __func__, pgcnt);
        mm_dump_rangelist();
    }
#if MEMORY_DEBUG
    if (!mm_sanity_check_is_valid_page(first, false)) {
        printf("%s ERROR invoked with invalid first page %p - count = %d\n", __func__, first, pgcnt);
    }
    if (!mm_sanity_check_is_valid_page(last, false)) {
        printf("%s ERROR invoked with invalid last page %p - count = %d\n", __func__, last, pgcnt);
    }
    
    mm_sanity_check_tree_nodes_before();
#endif

    // TODO: needs lock
    
    int diff = INT_MIN;
    struct mm_rangelist *node;
    struct mm_rangelist *nearest = NULL;
    struct mm_page *pg;
    uint32_t min, max;

    mutex_enter(&pg_rangelist.pg_rangelist_lock);

    node = pg_rangelist.max;

    if (node == NULL) {
        node = pool_get(&rb_node_pool, 0);
        node->first_page = first;
        node->page_count = pgcnt;
        node->repeat = 0;
        pg_rangelist.max = node;
        pg_rangelist.min = node;
        pg = first;
        for (int i = 0; i < pgcnt; i++) {
            pg->pageq.tree.rb_tree = node;
            pg = pg->pageq.tree.li_next;
        }
        mutex_exit(&pg_rangelist.pg_rangelist_lock);
        return;
    }
    
    min = pg_rangelist.min->page_count;
    max = pg_rangelist.max->page_count;

    if (pgcnt > max) {
        node = pool_get(&rb_node_pool, 0);
        node->first_page = first;
        node->page_count = pgcnt;
        node->repeat = 0;
        node->prev = pg_rangelist.max;
        pg_rangelist.max->next = node;
        pg_rangelist.max = node;
        pg = first;
        for (int i = 0; i < pgcnt; i++) {
            pg->pageq.tree.rb_tree = node;
            pg = pg->pageq.tree.li_next;
        }
        mutex_exit(&pg_rangelist.pg_rangelist_lock);
        return;
    } else if (pgcnt < min) {
        node = pool_get(&rb_node_pool, 0);
        node->first_page = first;
        node->page_count = pgcnt;
        node->repeat = 0;
        node->next = pg_rangelist.min;
        pg_rangelist.min->prev = node;
        pg_rangelist.min = node;
        pg = first;
        for (int i = 0; i < pgcnt; i++) {
            pg->pageq.tree.rb_tree = node;
            pg = pg->pageq.tree.li_next;
        }
        mutex_exit(&pg_rangelist.pg_rangelist_lock);
        return;
    }

    nearest = mm_rangelist_ge_bottomup(pgcnt);

    if (nearest && nearest->page_count == pgcnt) {
        nearest->repeat++;
        pg = nearest->first_page;
        nearest->first_page = first;
        last->pageq.tree.li_next = pg;
        pg = first;
        for (int i = 0; i < pgcnt; i++) {
            pg->pageq.tree.rb_tree = node;
            pg = pg->pageq.tree.li_next;
        }
    } else {
#if MEMORY_DEBUG
        if (nearest == NULL) {
            printf("%s nearest is NULL\n" , __func__);
        }
#endif
        node = pool_get(&rb_node_pool, 0);
        node->first_page = first;
        node->page_count = pgcnt;
        node->repeat = 0;
        node->prev = nearest->prev;
        node->next = nearest;
        if (node->prev)
            node->prev->next = node;
        nearest->prev = node;
        pg = first;
        for (int i = 0; i < pgcnt; i++) {
            pg->pageq.tree.rb_tree = node;
            pg = pg->pageq.tree.li_next;
        }
    }

    mutex_exit(&pg_rangelist.pg_rangelist_lock);

#if MEMORY_DEBUG
    mm_sanity_check_tree_nodes_after();
#endif
}

static void mm_put_page_single(struct mm_page *);

void
mm_dump_rangelist(void)
{
    struct mm_rangelist *node;

    printf("dump range-list (start) min = %p max = %p", pg_rangelist.min, pg_rangelist.max);

    mutex_enter(&pg_rangelist.pg_rangelist_lock);

    node = pg_rangelist.max;
    while (node != NULL) {
        printf("\t{%p page-count: %d repeat: %d first-page %p fp->phys_addr %p  prev: %p next: %p}", node, node->page_count, node->repeat, node->first_page, (void *)node->first_page->phys_addr, node->prev, node->next);
        node = node->prev;
    }

    mutex_exit(&pg_rangelist.pg_rangelist_lock);
}

static struct mm_page * __noinline
mm_get_page_rangelist(struct mm_pg_range_list *rlist, uint32_t pgcnt)
{
    int diff = INT_MIN;
    struct mm_rangelist *node;
    struct mm_rangelist *nearest = NULL;
    struct mm_rangelist *split = NULL;
    struct mm_page *last, *pg;
    struct mm_page *range_start, *range_end, *new_first, *new_last;
    uint32_t min, max, newpgc, repeat;

    pg = NULL;
    mutex_enter(&pg_rangelist.pg_rangelist_lock);

#if MEMORY_DEBUG
    mm_sanity_check_tree_nodes_before();
#endif

    if (pg_rangelist.max == NULL || pgcnt > pg_rangelist.max->page_count) {
        mutex_exit(&pg_rangelist.pg_rangelist_lock);
        return NULL;
    }

    node = mm_rangelist_ge_bottomup(pgcnt);
    if (node == NULL) {
        printf("%s ERROR not enough free memory of range %d\n", __func__, pgcnt);
        return NULL;
    }
    repeat = node->repeat;
    newpgc = node->page_count - pgcnt;

    if (newpgc == 0) {

#if __WASM_KERN_DEBUG_PRINT
        printf("%s found exact match %p\n", __func__, node);
#endif

        if (node->repeat == 0) {
            if (node->prev)
                node->prev->next = node->next;
            if (node->next)
                node->next->prev = node->prev;
            pg = node->first_page;

            mutex_exit(&pg_rangelist.pg_rangelist_lock);

            mm_move_page_range(node->first_page, NULL, pgcnt);
            node->first_page = NULL;

            if (rlist->min == node) {
                rlist->min = node->next;
            }
            if (rlist->max == node) {
                rlist->max = node->prev;
            }

            pool_put(&rb_node_pool, node);

        } else {

            pg = node->first_page;

            last = mm_move_page_range(pg, NULL, pgcnt);

            node->repeat--;
            node->first_page = last->pageq.tree.li_next;
            last->pageq.tree.li_next = NULL;

            mutex_exit(&pg_rangelist.pg_rangelist_lock);
        }

#if MEMORY_DEBUG
        mm_sanity_check_tree_nodes_after();
#endif

        return pg;

    } else if (newpgc == 1) {
        // optimizing for dropping to single page list.

        // unset pointer to rangelist
        pg = node->first_page;
        if (pg == NULL) {
            printf("%s pg == NULL!!\n", __func__);
            mm_dump_rangelist();
        }
        range_end = mm_move_page_range(pg, NULL, pgcnt);
        new_first = range_end->pageq.tree.li_next;
        range_end->pageq.tree.li_next = NULL;

#if __WASM_KERN_DEBUG_PRINT
        printf("%s found nearest match %p new page-count after alloc: %d\n", __func__, node, newpgc);
#endif

        if (repeat == 0) {
            // detatch self from current location.
            mm_rangelist_detatch(&pg_rangelist, node);
            pool_put(&rb_node_pool, node);

        } else {
            node->first_page = new_first->pageq.tree.li_next;
            node->repeat--;
            new_first->pageq.list.li_next = NULL;
            new_first->pageq.list.li_prev = NULL;
        }

#if __WASM_KERN_DEBUG_PRINT
        printf("%s dropping to single page array", __func__);
#endif
        
        mm_put_page_single(new_first);

        mutex_exit(&pg_rangelist.pg_rangelist_lock);

#if MEMORY_DEBUG
        mm_sanity_check_tree_nodes_after();
#endif

        return pg;

    } else if (repeat == 0 && (node->prev == NULL || node->prev->page_count < newpgc)) {
        // optimizing for no movement in the rangelist.

        pg = node->first_page;
        if (pg == NULL) {
            printf("%s pg == NULL!!\n", __func__);
            mm_dump_rangelist();
        }
        range_end = mm_move_page_range(pg, NULL, pgcnt);
        new_first = range_end->pageq.tree.li_next;
        range_end->pageq.tree.li_next = NULL;

        node->first_page = new_first;
        node->page_count = newpgc;

        mutex_exit(&pg_rangelist.pg_rangelist_lock);

#if MEMORY_DEBUG
        mm_sanity_check_tree_nodes_after();
#endif

        return pg;

    } else {

        // unset pointer to rangelist
        pg = node->first_page;
        if (pg == NULL) {
            printf("%s ERROR pg == NULL!!\n", __func__);
            mm_dump_rangelist();
        }
        range_end = mm_move_page_range(pg, NULL, pgcnt);
        new_first = range_end->pageq.tree.li_next;
        range_end->pageq.tree.li_next = NULL;

#if __WASM_KERN_DEBUG_PRINT
        printf("%s found nearest match %p new page-count after alloc: %d\n", __func__, node, newpgc);
#endif

        nearest = mm_rangelist_ge_below(node, newpgc);
        if (nearest == NULL) {
            nearest = pg_rangelist.min;
        }

#if __WASM_KERN_DEBUG_PRINT
        printf("%s insert before %p", __func__, nearest);
#endif

        if (nearest == node && node->repeat == 0) {

            last = mm_move_page_range(new_first, nearest, newpgc);

            node->first_page = new_first;
            node->page_count = newpgc;
        } else if (nearest->page_count == newpgc) {

            last = mm_move_page_range(new_first, nearest, newpgc);

            if (repeat == 0) {
                mm_rangelist_detatch(&pg_rangelist, node);
                pool_put(&rb_node_pool, node);
            } else {
                node->first_page = last->pageq.tree.li_next;
                node->repeat--;
            }

            last->pageq.tree.li_next = nearest->first_page;
            nearest->first_page = new_first;
            nearest->repeat++;

        } else {

            if (repeat == 0) {
                node->first_page = new_first;
                node->page_count = newpgc;
                // detatch self from current location.
                mm_rangelist_detatch(&pg_rangelist, node);
                split = node;
            } else {
                split = pool_get(&rb_node_pool, 0);
                split->repeat = 0;
                split->first_page = new_first;
                split->page_count = newpgc;

                new_last = mm_move_page_range(new_first, split, newpgc);
                node->first_page = new_last->pageq.tree.li_next;
                node->repeat--;
                new_last->pageq.tree.li_next = NULL;
            }

            split->next = nearest;
            split->prev = nearest->prev;
            if (nearest->prev)
                nearest->prev->next = split;
            nearest->prev = split;

            if (newpgc < pg_rangelist.min->page_count) {
                pg_rangelist.min = split;
            }

            if (split->next == NULL) {
                printf("%s ERROR split->next is NULL\n", __func__);
            }
        }
    
        mutex_exit(&pg_rangelist.pg_rangelist_lock);

#if MEMORY_DEBUG
        mm_sanity_check_tree_nodes_after();
#endif

        return pg;
    }

}


static void *
rangelist_pool_alloc(struct pool *pool, int size)
{
    return NULL;
}

static void
rangelist_pool_free(struct pool *pool, void *ptr)
{

}

// handle sinle free pages

/**
 * Flags must be cleared by caller.
 */
static void
mm_put_page_single(struct mm_page *pg)
{
    mutex_enter(&dcon_pg_lock);

    pg->flags |= (PG_FREE|PG_FREELIST);

    if (dcon_pg_list == NULL) {
        pg->pageq.list.li_next = pg;
        pg->pageq.list.li_prev = pg;
    } else {
        pg->pageq.list.li_next = dcon_pg_list->pageq.list.li_next;
        pg->pageq.list.li_prev = dcon_pg_list->pageq.list.li_prev;
    }

    dcon_pg_list = pg;
    
    mutex_exit(&dcon_pg_lock);

    atomic_add32(&dcon_pg_cnt, 1);
}

static struct mm_page *
mm_get_single_page(void)
{
    struct mm_page *nf, *pg;

    mutex_enter(&dcon_pg_lock);

    if (dcon_pg_list != NULL) {
        pg = dcon_pg_list;
        if (pg->pageq.list.li_next != pg) {
            nf = pg->pageq.list.li_next;
            pg->pageq.list.li_next->pageq.list.li_prev = pg->pageq.list.li_prev;
            pg->pageq.list.li_prev->pageq.list.li_next = pg->pageq.list.li_next;
        } else {
            nf = NULL;
        }

        mutex_exit(&dcon_pg_lock);

        pg->pageq.list.li_prev = NULL;
        pg->pageq.list.li_next = NULL;
        pg->flags &= ~(PG_FREE|PG_FREELIST);
        dcon_pg_list = nf;

        atomic_sub32(&dcon_pg_cnt, 1);
    } else {
        mutex_exit(&dcon_pg_lock);
        pg = NULL;
    }

    return pg;
}

static int
mm_disconnect_single_page(struct mm_page *pg)
{
    mutex_enter(&dcon_pg_lock);

    if (pg->pageq.list.li_next != pg) {
        pg->pageq.list.li_next->pageq.list.li_prev = pg->pageq.list.li_prev;
        pg->pageq.list.li_prev->pageq.list.li_next = pg->pageq.list.li_next;
    }

    mutex_exit(&dcon_pg_lock);

    return 0;
}

// must be called with lock held
static int
mm_disconnect_page_range(struct mm_page *pg, struct mm_page **first, struct mm_page **last)
{
    struct mm_rangelist *list = pg->pageq.tree.rb_tree;
    struct mm_page *start;
    struct mm_page *end;
    struct mm_page *prev;
    uintptr_t phys_addr;
    uint32_t len;
    uint32_t pg_count;
    int idx;

    if (list == NULL) {
        goto error;
    }

#if MEMORY_DEBUG
    mm_sanity_check_tree_nodes_before();
#endif

    pg_count = list->page_count;
    start = list->first_page;

    if (list->repeat == 0) {
        idx = atop(start->phys_addr);
        end = pg_avec[idx + pg_count - 1];

        mm_rangelist_detatch(&pg_rangelist, list);
        pool_put(&rb_node_pool, list);

        if (first)
            *first = start;
        if (last)
            *last = end;

        return pg_count;
    }

    len = list->repeat + 1;
    phys_addr = pg->phys_addr;
    prev = NULL;

    for (int i = 0; i < len; i++) {
        idx = atop(start->phys_addr);
        end = pg_avec[idx + pg_count - 1];
        if (start->phys_addr >= phys_addr && phys_addr <= end->phys_addr) {

            if (prev == NULL) {
                list->first_page = end->pageq.tree.li_next;
            } else {
                prev->pageq.tree.li_next = end->pageq.tree.li_next;
            }

            break;
        }

        start = end->pageq.tree.li_next;
        prev = end;
    }

    end->pageq.tree.li_next = NULL;
    list->repeat--;

    if (first)
        *first = start;
    if (last)
        *last = end;

#if MEMORY_DEBUG
    mm_sanity_check_tree_nodes_after();
#endif

    return pg_count;

error:
    if (first)
        *first = NULL;
    if (last)
        *last = NULL;

#if MEMORY_DEBUG
    mm_sanity_check_tree_nodes_after();
#endif

    return -1;
}

// init memory

struct init_pg_bucket {
    uint32_t pgcnt;             // number of pages needed in bucket.
    struct mm_phys_seg *seg;    //
    uintptr_t addr;             //
    uintptr_t min_addr;         // min address to be placed at.
    bool tail;
};

#define	BITMAP_SIZE	(CHAR_BIT * sizeof(pool_item_bitmap_t))

static void
pr_item_bitmap_init_early(const struct pool *pp, struct pool_item_header *ph)
{
	pool_item_bitmap_t *bitmap = ph->ph_u2.phu_notouch.phu_bitmap;
	const int n = howmany(pp->pr_itemsperpage, BITMAP_SIZE);
	int i;

	for (i = 0; i < n; i++) {
		bitmap[i] = (pool_item_bitmap_t)-1;
	}
}

static void
rb_pool_init(struct pool *rb_pool, struct mm_phys_seg *seg, uint32_t pgcnt)
{
    pool_init(rb_pool, sizeof(struct mm_rangelist), 4, 0, PR_USEBMAP, "mm_rbnode", &pool_allocator_rangelist, IPL_NONE);
    rb_pool->pr_minitems = 0;
    rb_pool->pr_nitems = pgcnt * 169;
    rb_pool->pr_minpages = pgcnt;
    rb_pool->pr_npages = pgcnt;
    

    struct pool_item_header *pool_head, *first_head, *last_head;
    struct mm_page *pg = seg->first;
    first_head = NULL;
    rb_pool->pr_roflags &= ~(PR_PHINPAGE); 
    rb_pool->pr_roflags |= PR_USEBMAP;
    rb_pool->pr_itemoffset = 0;
    rb_pool->pr_itemsperpage = 169;
    last_head = NULL;

    printf("seg->first %p\n", pg);

    for (int i = 0; i < pgcnt; i++) {
        pool_head = (struct pool_item_header *)(rb_pool_items_buf + (56 * i));
        pool_head->ph_off = 0;
        pool_head->ph_page = (void *)pg->phys_addr;
        pool_head->ph_nmissing = 0;
        if (first_head == NULL) {
            first_head = pool_head;
        }

        printf("pool_item_header: %p ph_page at %p\n", pool_head, pool_head->ph_page);

        pr_item_bitmap_init_early(rb_pool, pool_head);
        LIST_INSERT_HEAD(&rb_pool->pr_emptypages, pool_head, ph_pagelist);
        pg = pg->pageq.list.li_next;

        SPLAY_INSERT(phtree, &rb_pool->pr_phtree, pool_head);
    }

    rb_pool->pr_curpage = first_head;
}

void test_wasm_mm(void);
void test_wasm_malloc(void);
void mm_kmem_init(void);

void
init_wasm_memory(void)
{
    struct wasm_bootstrap_info *info;
    void *tstackp;
    uint32_t pgcnt, pgsrnd, pgdatasz, pgmetapgs, pgarrsz, pg_arr_pgs, rsvd_raw, rsvd_pad, rsvd_pgs, bucket_cnt;
    uint32_t rb_cnt;
    int32_t rem;
    int32_t wapgs = wasm_memory_size();
    int32_t avail = wapgs * WASM_PAGE_SIZE;
    int32_t rsvdraw = 0;
    struct btinfo_memmap *bt_mmap = lookup_bootinfo(BTINFO_MEMMAP);
    uintptr_t metaptr;  // meta table start addr.
    uintptr_t pvecptr;  // page-vector table start addr.
    struct mm_phys_seg *metaseg;    // index of phys-seg that fit meta table.
    struct mm_phys_seg *pvecseg;    // index of phys-seg that fit page-array table.
    struct mm_phys_seg *iomemseg;

    struct init_pg_bucket buckets[5];
    struct init_pg_bucket *meta_bucket = &buckets[0];
    struct init_pg_bucket *pvec_bucket = &buckets[1];
    struct init_pg_bucket *pgrb_bucket = &buckets[2];
    struct init_pg_bucket *msgb_bucket = &buckets[3];
    bucket_cnt = 4;

    wasm_memory_fill(&buckets[0], 0x00, sizeof(buckets));

    pgrb_bucket->pgcnt = 6; // 1024 mm_rangelist items

    msgb_bucket->pgcnt = howmany(MSGBUFSIZE, PAGE_SIZE);
    msgb_bucket->min_addr = IOM_END;
    msgb_bucket->tail = true;


    mutex_init(&__malloc_mtx, MUTEX_SPIN, 0);
    mutex_init(&__uvmspaces_lock, MUTEX_SPIN, 0);

    if (bt_mmap->memory_max == WASM_MEMORY_UNLIMITED) {
        wasm_kmem_limit = WASM_MEMORY_UNLIMITED;
    } else {
        wasm_kmem_limit = bt_mmap->memory_max * WASM_PAGE_SIZE;
        physmem = wasm_kmem_limit;
    }

    wasm_kmem_growable = (avail != wasm_kmem_limit);
    wasm_kmem_avail = avail;
    avail_end = avail;

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

    pvec_bucket->pgcnt = pg_arr_pgs;
    meta_bucket->pgcnt = pgmetapgs;

    printf("should alloc %d mm pages, data-size: %d (%d pages) rounded cnt: %d page-array-size: %d page-array-pages: %d\n", pgcnt, pgdatasz, pgmetapgs, pgsrnd, pgarrsz, pg_arr_pgs);

    lastseg = NULL;
    metaseg = NULL;
    pvecseg = NULL;
    iomemseg = NULL;
    seg = firstseg;
    for (int x = 0; x < bucket_cnt; x++) {
        struct init_pg_bucket *bucket = &buckets[x];
        uint32_t pgcnt = bucket->pgcnt;
        if (bucket->seg != NULL || pgcnt == 0) {
            continue;
        }

        seg = firstseg;
        for (int i = 0; i < si; i++) {

            if (iomemseg == NULL && seg->type == BIM_IOMEM) {
                iomemseg = seg;
            }

            if (seg->type != BIM_Memory || (seg->pgflags & PG_BUSY) != 0 || seg->pg_start < bucket->min_addr) {
                lastseg = seg;
                seg = seg->next;
                continue;
            }

            if (seg->pg_cnt >= pgcnt) {

                if (seg->pg_cnt == pgcnt) {
                    // consume segment
                    bucket->addr = seg->pg_start;
                    bucket->seg = seg;
                    break;
                } else {

                    if (bucket->tail != true) {
                        // split segment
                        nseg = &segs[si++];
                        nseg->type = BIM_Memory;
                        nseg->pgbacked = 1;
                        nseg->pgflags = PG_BUSY;
                        nseg->next = seg;
                        lastseg->next = nseg;
                        nseg->pg_start = seg->pg_start;
                        nseg->start = nseg->pg_start;
                        nseg->pg_cnt = pgcnt;
                        nseg->pg_end = nseg->pg_start + (pgcnt * PAGE_SIZE);
                        nseg->end = nseg->pg_end;
                        seg->pg_start = nseg->pg_end;
                        seg->start = seg->pg_start;
                        seg->pg_cnt -= pgcnt;
                        bucket->addr = nseg->pg_start;
                        bucket->seg = nseg;
                        lastseg = nseg;
                        break;
                    } else {
                        // split segment insert new at tail
                        nseg = &segs[si++];
                        nseg->type = BIM_Memory;
                        nseg->pgbacked = 1;
                        nseg->pgflags = PG_BUSY;
                        nseg->next = seg->next;
                        seg->next = nseg;
                        nseg->pg_start = seg->pg_end - (pgcnt * PAGE_SIZE);;
                        nseg->start = nseg->pg_start;
                        nseg->pg_cnt = pgcnt;
                        nseg->pg_end = seg->pg_end;
                        nseg->end = nseg->pg_end;
                        seg->pg_end = nseg->pg_start;
                        seg->end = seg->pg_end;
                        seg->pg_cnt -= pgcnt;
                        bucket->addr = nseg->pg_start;
                        bucket->seg = nseg;
                        lastseg = nseg;
                        break;
                    }
                }
            }
            
            lastseg = seg;
            seg = seg->next;
        }
    }

    // DEBUG PRINT
    printf("before pg init\n");
    seg = firstseg;
    for (int i = 0; i < si; i++) {
        printf("{type %d page-backed: %s start %lu pg_start %lu end %lu pg_end %lu pg_cnt %u next: %p}", seg->type, seg->pgbacked ? "YES" : "NO", seg->start, seg->pg_start, seg->end, seg->pg_end, seg->pg_cnt, seg->next);
        seg = seg->next;
    }

    struct mm_page *first, *prev, *pg;
    pg = (struct mm_page *)meta_bucket->addr;
    first = pg;
    prev = NULL;

    printf("metaptr = %p pgvec-ptr %p\n", first, (void *)pvec_bucket->addr);

    printf("meta_bucket addr: %p first page: %p\n", (void *)meta_bucket->addr, meta_bucket->seg->first);
    printf("pvec_bucket addr: %p first page: %p\n", (void *)pvec_bucket->addr, pvec_bucket->seg->first);
    printf("pgrb_bucket addr: %p first page: %p\n", (void *)pgrb_bucket->addr, pgrb_bucket->seg->first);

    for (int i = 0; i < pgsrnd; i++) {
        pg->pgsz = 2;
        pg->flags = 0;
        pg->pageq.list.li_prev = prev;
        if (prev)
            prev->pageq.list.li_next = pg;
        prev = pg;
        pg++;
    }

    printf("after pg init (start)\n");
    seg = firstseg;
    for (int i = 0; i < si; i++) {
        printf("{type %d page-backed: %s start %lu pg_start %lu end %lu pg_end %lu pg_cnt %u next: %p}", seg->type, seg->pgbacked ? "YES" : "NO", seg->start, seg->pg_start, seg->end, seg->pg_end, seg->pg_cnt, seg->next);
        seg = seg->next;
    }
    printf("after pg init (end)\n");

    struct mm_page **pgavec_start = (struct mm_page **)pvec_bucket->addr;
    pg = first;
    seg = firstseg;

#if MEMORY_DEBUG
        pg_first_addr = first;
        printf("%s first-page %p\n", __func__, first);
#endif

    for (int x = 0; x < si; x++) {

        if (seg->pgbacked != TRUE) {
            seg = seg->next;
            continue;
        }

        struct mm_page **pgavec = pgavec_start + (seg->pg_start / PAGE_SIZE);
        struct mm_page *firstpg, *lastpg;
        uintptr_t phys_addr = seg->pg_start;
        uint32_t segpgsz = PAGE_SIZE;
        uint16_t pgflags;
        int ylen = seg->pg_cnt;

        printf("pgavec: %p\n", pgavec);

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
            *pgavec = pg;
            lastpg = pg;
            pgavec++;
            pg++;
        }

        seg->first = firstpg;
        seg->last = lastpg;

        seg = seg->next;
    }

#if MEMORY_DEBUG
        pg_last_addr = pg;
        printf("%s last-page %p\n", __func__, pg);
#endif

    // kick-start the rb node pool
    // a mm_rbnode is 24 bytes, 170 whole items would fit into a single page, 24 bytes would be required for bitmap
    // that maps allocation within the page so 169 items and a bitmap would fit inside a page.
    rb_pool_init(&rb_node_pool, pgrb_bucket->seg, pgrb_bucket->pgcnt);

    // range-list init

    seg = firstseg;
    for (int x = 0; x < si; x++) {
        
        if (seg->type != BIM_Memory || (seg->pgflags & PG_BUSY) != 0) {
            seg = seg->next;
            rsvdraw += seg->end - seg->start;
            continue;
        }

        if (seg->pg_cnt == 1) {
            mm_put_page_single(seg->first);
        } else {
            mm_put_page_range(&pg_rangelist, seg->first, seg->last, seg->pg_cnt);
        }

        seg = seg->next;
    }

    // finishing up
    pg_avec = pgavec_start;
    pg_avecsz = pgarrsz / sizeof(void *);

    // disconnect busy pages
    seg = meta_bucket->seg;
    pg_detach_list(seg->first, seg->last);
    pg_metatbl = seg->first;

    seg = pvec_bucket->seg;
    pg_detach_list(seg->first, seg->last);

    seg = msgb_bucket->seg;
    pg_detach_list(seg->first, seg->last);
    msgbuf_vaddr = seg->first->phys_addr;
    msgbuf_p_cnt = seg->pg_cnt;

    if (iomemseg)
        pg_detach_list(iomemseg->first, iomemseg->last);

    uint32_t pages_busy = 0;
    pages_busy += meta_bucket->pgcnt;
    pages_busy += pvec_bucket->pgcnt;
    pages_busy += pgrb_bucket->pgcnt;
    pages_busy += msgb_bucket->pgcnt;

    atomic_store32(&__kmem_data.unmapped_raw, rsvd_raw);
    atomic_store32(&__kmem_data.unmapped_pad, rsvd_pad);
    atomic_store32(&__kmem_data.rsvd_pgs, rsvd_pgs);
    atomic_store32(&__kmem_data.pages_total, pgcnt);
    atomic_store32(&__kmem_data.pages_busy, pages_busy);

    atomic_store32((uint32_t *)&nkmempages, pgcnt);
    

    // Put it here temporary!
	test_wasm_mm();

    // initialzing kernel mm_space
    pool_init(&mm_space_pool, sizeof(struct vm_space_wasm), 4, 0, 0, "mm_space", &pool_allocator_mm_space, IPL_NONE);
    __wasm_kmeminfo = pool_get(&mm_space_pool, 0);

    printf("__wasm_kmeminfo = %p\n", __wasm_kmeminfo);

    void *argp[2];
    argp[0] = &__mmblkd_head;
    argp[1] = __wasm_kmeminfo;
    __wasm_kexec_ioctl(552, argp);

    __wasm_kmeminfo->uspace = 1;
    __wasm_kmeminfo->refcount = 1;
    __wasm_kmeminfo->ptrsz = sizeof(void *);
    __wasm_kmeminfo->reserved = avail;
    __wasm_kmeminfo->maximum = wasm_kmem_limit;
    curlwp->l_md.md_kmem = __wasm_kmeminfo;
    curlwp->l_md.md_umem = NULL;

    mm_dump_rangelist();

    // testing rb_node_pool
    struct mm_rangelist *t3, *t2, *t1 = pool_get(&rb_node_pool, 0);
    printf("mm_rangelist #1 at %p\n", t1);
    t2 = pool_get(&rb_node_pool, 0);
    printf("mm_rangelist #2 at %p\n", t2);
    t3 = pool_get(&rb_node_pool, 0);
    printf("mm_rangelist #2 at %p\n", t3);

    // this usually called in uvm_init()
    pool_subsystem_init();

    mm_kmem_init();

    test_wasm_malloc();

    // usually called in uvm_init()
    rw_obj_init();
}

int grow_kernel_memory(unsigned int wapgs)
{
    uint32_t newval, new_avail_end;
    int32_t err, oldval;

    mutex_enter(&__kgrow_lock);

    oldval = wasm_memory_grow(wapgs);
    if (oldval != -1) {
        newval = oldval + wapgs;

        // only consider the range from oldval to newval, someone else might have grown memory, in which case
        // we mark this amount as reserved/unmapped

        if (wasm_kmem_avail != (oldval * WASM_PAGE_SIZE)) {

        }

        new_avail_end = newval * WASM_PAGE_SIZE;
        atomic_store32(&wasm_kmem_avail, new_avail_end);
        atomic_store32(&avail_end, new_avail_end);
        err = 0;
    } else {
        err = ENOMEM;
    }

    mutex_exit(&__kgrow_lock);

    return err;
}

int 
uvm_availmem(bool cached)
{
    return atomic_load32(&__kmem_data.pages_total) - atomic_load32(&__kmem_data.pages_busy);
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

void *
kmem_page_alloc(unsigned int pgs, km_flag_t flags)
{
    struct mm_page *pg, *cur;
    void *paddr = NULL;

    // TODO: use rb tree to organize pages by continuous range?
    if (pgs == 1 && atomic_load32(&dcon_pg_cnt) > 0) {
        pg = mm_get_single_page();
        if (pg != NULL) {
            if (pg->phys_addr == 0x883000) {
                printf("%s ERROR paddr == 0x883000\n", __func__);
            }
            pg->flags |= (PG_BUSY|PG_CLEAN|PG_FAKE);
            pg->flags &= ~(PG_FREE|PG_FREETREE|PG_FREELIST);

            paddr = (void *)pg->phys_addr;

            return paddr;
        }
    }

    pg = mm_get_page_rangelist(&pg_rangelist, pgs);
    if (pg == NULL) {
        goto error_out;
    }

    if (pg->phys_addr == 0) {
        printf("%s ERROR pg->phys_addr = NULL for page at %p\n", __func__, pg);
        mm_dump_rangelist();
    }

    atomic_add32(&__kmem_data.pages_busy, pgs);
    paddr = (void *)pg->phys_addr;

    pg->flags |= (PG_BUSY|PG_CLEAN|PG_FAKE);

    if (flags & UVM_PGA_ZERO) {
        wasm_memory_fill(paddr, 0, pgs * PAGE_SIZE);
    }

    uintptr_t idx = atop(pg->phys_addr);
    if (idx > 0 && idx < pg_avecsz) {
        struct mm_page *prev = pg_avec[idx - 1];
        if (prev != NULL && prev->pageq.list.li_next == pg) {
            prev->pageq.list.li_next = NULL;
        }
    }

    cur = pg;
    for (int i = 0; i < pgs; i++) {

        if (cur->phys_addr == 0x883000) {
            printf("%s ERROR paddr == 0x883000\n", __func__);
        }

        cur->flags |= (PG_BUSY|PG_CLEAN|PG_FAKE);
        //if ((cur->flags & PG_FREE) == 0 || ((cur->flags & PG_FREETREE) == 0) && (cur->flags & PG_FREELIST) == 0) {
        //    printf("%s ERROR found non free page memory alloc; pg = %p pg->phys_addr = %p\n", __func__, cur, (void *)cur->phys_addr);
        //}
        cur->flags &= ~(PG_FREE|PG_FREETREE|PG_FREELIST); // remove the free flags, [don't know if this should be done here..]
        cur = cur->pageq.queue.tqe_next;
    }

    return paddr;

error_out:

    printf("%s ERROR allocation failed pgcnt %d\n", __func__, pgs);
    mm_dump_rangelist();

    return paddr;
}

void *
kmem_page_zalloc(unsigned int pgs, km_flag_t flags)
{
    void *ptr = kmem_page_alloc(pgs, flags);
    if (ptr != NULL) {
        wasm_memory_fill(ptr, 0, pgs * PAGE_SIZE);
    }

    return ptr;
}

#if 0
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
    struct mm_page *before, *after;
    uint32_t pgcnt = 1;
    uint32_t tmp, idx = atop(first->phys_addr);
    before = idx > 0 ? pg_avec[idx - 1] : NULL;
    if (first != last) {
        tmp = idx;
        idx = atop(last->phys_addr);
        pgcnt = (idx - tmp) + 1;
    }
    after = idx < pg_avecsz ? pg_avec[idx + 1] : NULL;

    // TODO: freeing a page might result in a larger segment now being continous; check page before & after
    if (before && ((before->flags & PG_FREELIST) != 0 || (before->flags & PG_FREETREE) != 0)) {

    }

    if (after && ((after->flags & PG_FREELIST) != 0 || (after->flags & PG_FREETREE) != 0)) {
        
    }


    atomic_sub32(&__kmem_data.pages_busy, pgcnt);
}
#endif

void
kmem_page_free(void *addr, uint32_t pagecnt)
{

    struct mm_page *last, *tmp, *pg;
    struct mm_page *before, *after;
    struct mm_page *start, *end;
    bool before_hint, after_hint;
    uint32_t orgcnt = pagecnt;
    uint32_t idx;

    idx = atop(addr);
    pg = pg_avec[idx];

    if (pg == NULL || pg->phys_addr != (uintptr_t)addr) {
        printf("%s ERROR addr = %p does not match page %p (phys_addr = %p) mapping that addr at pg_avec[%d]\n", __func__, addr, pg, (void *)pg->phys_addr, idx);
        return;
    }
#if MEMORY_DEBUG
    mm_sanity_check_is_valid_page(pg, true);
#endif

#if __WASM_DEBUG_KERN_MEM
    printf("%s free page %p (%d) phys_addr: %p\n", __func__, pg, idx, (void *)pg->phys_addr);
#endif

    before_hint = false;
    after_hint = false;

    before = idx >= 0 ? pg_avec[idx - 1] : NULL;
    start = pg;
    if (pagecnt != 1) {
        idx = idx + pagecnt;
        end = pg_avec[idx - 1];
        after = idx < pg_avecsz ? pg_avec[idx] : NULL;
    } else {
        after = idx + 1 < pg_avecsz ? pg_avec[idx + 1] : NULL;
        end = pg;
    }

    if (before) {
        before_hint = (before->flags & PG_FREE) != 0 && ((before->flags & PG_FREELIST) != 0 || (before->flags & PG_FREETREE) != 0);
#if __WASM_DEBUG_KERN_MEM
        if (before_hint)
            printf("%s found free range before\n", __func__);
#endif
    }

    if (after) {
        after_hint = (after->flags & PG_FREE) != 0 && (((after->flags & PG_FREELIST) != 0 || (after->flags & PG_FREETREE) != 0));
#if __WASM_DEBUG_KERN_MEM
        if (after_hint)
            printf("%s found free range after\n", __func__);
#endif
    }

#if 0
    if (before_hint || after_hint) {

        struct mm_page *range_start, *range_end;
        uint32_t rcnt;

        range_start = NULL;
        range_end = NULL;

        mutex_enter(&pg_rangelist.pg_rangelist_lock);

        if (mutex_tryenter(&before->interlock)) {

            if ((before->flags & PG_FREETREE) != 0) {
                rcnt = mm_disconnect_page_range(before, &range_start, &range_end);
                if (rcnt > 0) {
                    if (range_start == NULL || range_end == NULL) {
                        printf("%s got either NULL start or end.. before = %p range_start = %p range_end = %p\n", __func__, before, range_start, range_end);
                    } else {
                        range_end->pageq.tree.li_next = pg;
                        start = range_start;
                        pagecnt += rcnt;
                    }
                }
            } else if ((before->flags & PG_FREELIST) != 0) {
                mm_disconnect_single_page(before);
                before->pageq.tree.li_next = pg;
                before->flags &= ~(PG_FREELIST);
                before->flags |= PG_FREETREE;
                start = before;
                pagecnt++;
            }
            mutex_exit(&before->interlock);
        } else {
            printf("%s mutex_tryenter(before) failed skipping merge\n", __func__);
        }

        if (mutex_tryenter(&after->interlock)) {

            if ((after->flags & PG_FREETREE) != 0) {
                rcnt = mm_disconnect_page_range(after, &range_start, &range_end);
                if (rcnt > 0) {
                    if (range_start == NULL || range_end == NULL) {
                        printf("%s got either NULL start or end.. after = %p range_start = %p range_end = %p\n", __func__, after, range_start, range_end);
                    } else {
                        end->pageq.tree.li_next = range_start;
                        end = range_end;
                        pagecnt += rcnt;
                    }
                }
            } else if ((after->flags & PG_FREELIST) != 0) {
                mm_disconnect_single_page(after);
                end->pageq.tree.li_next = after;
                end = after;
                after->flags &= ~(PG_FREELIST);
                after->flags |= PG_FREETREE;
                pagecnt++;
            }
            mutex_exit(&after->interlock);
        } else {
            printf("%s mutex_tryenter(after) failed skipping merge\n", __func__);
        }

        mutex_exit(&pg_rangelist.pg_rangelist_lock);
    }
#endif

    if (pagecnt == 1) {
        start->flags &= ~(PG_BUSY);
        start->flags |= (PG_FREE|PG_FREELIST);
        mm_put_page_single(start);
    } else {

        tmp = pg;
        for (int i = 0; i < orgcnt; i++) {
            if (tmp == NULL) {
                printf("ERROR broken page segment %p at index: %d of %d\n", tmp, i, orgcnt);
                break;
            }

            tmp->flags &= ~(PG_BUSY);
            tmp->flags |= (PG_FREE|PG_FREETREE);

            tmp = pg->pageq.tree.li_next;
        }

        mm_put_page_range(NULL, start, end, pagecnt);
    }

    atomic_sub32(&__kmem_data.pages_busy, pagecnt);
}

// mm_space

static void *
mm_space_pool_alloc(struct pool *pp, int flags)
{
#if __WASM_DEBUG_KERN_MEM
    printf("%s %p\n", __func__, pp);
#endif
    return kmem_page_alloc(1, 0);
}

static void
mm_space_pool_free(struct pool *pp, void *ptr)
{
#if __WASM_DEBUG_KERN_MEM
    printf("%s %p\n", __func__, pp);
#endif
    kmem_page_free(ptr, 1);
}

struct vm_space_wasm *
new_uvmspace(void)
{
    return pool_get(&mm_space_pool, 0);
}

static void
free_uvmspace(struct vm_space_wasm *spacep)
{
    pool_put(&mm_space_pool, spacep);
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

// Gets the first page in connected continous range.
void *
kmem_first_in_list(void *addr)
{
    struct mm_page *page;
    struct mm_page *prev;
    uintptr_t idx = atop(addr);
    if (idx >= pg_avecsz) {
        return NULL;
    }
    prev = pg_avec[idx];
    page = prev;
    while (idx > 0) {
        
        prev = pg_avec[idx - 1];
        if (prev == NULL || prev->pageq.list.li_next != page) {
            return (void *)page->phys_addr;
        }
        page = prev;
        idx--;
    }

    return (void *)page->phys_addr;
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

// uvm helpers

/*
 * PHYS_TO_VM_PAGE: find vm_page for a PA.   used by MI code to get vm_pages
 * back from an I/O mapping (ugh!).   used in some MD code as well.
 */
struct vm_page *
uvm_phys_to_vm_page(paddr_t pa)
{
    return (struct vm_page *)paddr_to_page((void *)pa);
}

paddr_t
uvm_vm_page_to_phys(const struct vm_page *pg)
{
	return pg->phys_addr & ~(PAGE_SIZE - 1);
}

/**
 * fills a page's memory with zeros.
 */
void
uvm_pagezero(struct vm_page *pg)
{
    uint32_t pgsz;
    if (pg == NULL)
        return;
    pgsz = mm_pgsztbl[pg->pgsz];
    wasm_memory_fill((void *)pg->phys_addr, 0, pgsz);
}

/**
 * get the dynamic page-size
 */
uint32_t
uvm_pagesize(struct mm_page *pg)
{
    return mm_pgsztbl[pg->pgsz];
}

void
uvm_pagewait(struct vm_page *pg, krwlock_t *lock, const char *wmesg)
{
    // TODO: wasm fixme
#if 0
	KASSERT(rw_lock_held(lock));
	KASSERT((pg->flags & PG_BUSY) != 0);
	KASSERT(uvm_page_owner_locked_p(pg, false));

	mutex_enter(&pg->interlock);
	pg->flags |= PQ_WANTED;
	rw_exit(lock);
	UVM_UNLOCK_AND_WAIT(pg, &pg->interlock, false, wmesg, 0);
#endif
}

/*
 * uvm_pagelock: acquire page interlock
 */
void
uvm_pagelock(struct vm_page *pg)
{

	mutex_enter(&pg->interlock);
}

/*
 * uvm_pagelock: acquire page interlock
 */
void
uvm_pageunlock(struct vm_page *pg)
{

	mutex_exit(&pg->interlock);
}

void
uvm_wait(const char *arg)
{
    // dummy
}

/*
 * uvm_aio_aiodone: do iodone processing for async i/os.
 * this should be called in thread context, not interrupt context.
 */
void
uvm_aio_aiodone(struct buf *bp)
{
	// TODO: fixme
	printf("%s fixme!\n", __func__);
	__panic_abort();
}

/*
 * uvmspace_free: free a vmspace data structure
 */

void
uvmspace_free(struct vmspace *vm)
{
	// TODO: fixme
	printf("%s fixme! vmspace %p not freed!\n", __func__, vm);
}

void
uvmspace_addref(struct vmspace *vm)
{
	// TODO: fixme replace references with the memidx mapped container
}

void
uvm_page_unbusy(struct vm_page **pgs, int npgs)
{
    // TODO: fixme
}

/*
 * uvm_pagewakeup: wake anyone waiting on a page
 *
 * => page interlock must be held
 */
void
uvm_pagewakeup(struct vm_page *pg)
{
    // TODO: fixme
}

void 
uvm_pagedeactivate(struct vm_page *pg)
{

}

void 
uvm_pageactivate(struct vm_page *pg)
{
    
}

vaddr_t
uvm_pagermapin(struct vm_page **pps, int npages, int flags)
{
    __panic_abort();
}
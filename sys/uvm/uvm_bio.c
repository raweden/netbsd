/*	$NetBSD: uvm_bio.c,v 1.128 2023/04/09 09:00:56 riastradh Exp $	*/

/*
 * Copyright (c) 1998 Chuck Silvers.
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * uvm_bio.c: buffered i/o object mapping cache
 */


#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: uvm_bio.c,v 1.128 2023/04/09 09:00:56 riastradh Exp $");

#include "opt_uvmhist.h"
#include "opt_ubc.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/vnode.h>
#include <sys/bitops.h>		/* for ilog2() */

#include <uvm/uvm.h>
#include <uvm/uvm_pdpolicy.h>

#ifdef __WASM
#include <wasm/wasm-extra.h>
#include <wasm/../mm/mm.h>
#include <wasm/wasm/builtin.h>
#define UBC_NO_HASH 1
#define __WASM_KERN_DEBUG_BIO 1
#endif

#ifdef PMAP_DIRECT
#  define UBC_USE_PMAP_DIRECT
#endif

#define UBC_WIN_NPAGES 2

#if defined(UBC_WIN_NPAGES) && UBC_WIN_NPAGES <= 4
#define UBC_MAP_HAS_PAGE_ARRAY 1
#endif

#ifdef UBC_WIN_PAGES_PREALLOC
extern struct mm_page *__ubc_page_bucket;
#endif

#define UBC_MAP_DEBUG 0


#if defined(UBC_MAP_DEBUG) && UBC_MAP_DEBUG != 0
#define ubc_dbg(...) printf(__VA_ARGS__)
#else
// if not do nothing
#define ubc_dbg(...)	
#endif

/*
 * local functions
 */

static int	ubc_fault(struct uvm_faultinfo *, vaddr_t, struct vm_page **,
			  int, int, vm_prot_t, int);
static struct ubc_map *ubc_find_mapping(struct uvm_object *, voff_t);
#ifndef UBC_NO_HASH
static int	ubchash_stats(struct hashstat_sysctl *hs, bool fill);
#endif
#ifdef UBC_USE_PMAP_DIRECT
static int __noinline ubc_uiomove_direct(struct uvm_object *, struct uio *, vsize_t,
			  int, int);
static void __noinline ubc_zerorange_direct(struct uvm_object *, off_t, size_t, int);

/* XXX disabled by default until the kinks are worked out. */
bool ubc_direct = false;
#endif

/*
 * local data structures
 */

#define UBC_HASH(uobj, offset) 											\
	(((((u_long)(uobj)) >> 8) + (((u_long)(offset)) >> PAGE_SHIFT)) & 	\
				ubc_object.hashmask)

#define UBC_QUEUE(offset)											\
	(&ubc_object.inactive[(((u_long)(offset)) >> ubc_winshift) &	\
			     (UBC_NQUEUES - 1)])

#define UBC_UMAP_ADDR(u)						\
	(vaddr_t)(ubc_object.kva + (((u) - ubc_object.umap) << ubc_winshift))


#define UMAP_PAGES_LOCKED	0x0001
#define UMAP_MAPPING_CACHED	0x0002
#define UMAP_INACTIVE_QUEUE	0x0010	/* indicates that the ubc is on the inactive queue (this would otherwise require a check if one of tree pointers is non-null)*/

struct ubc_map {
	struct uvm_object *	uobj;		/* mapped object */
	voff_t			offset;		/* offset into uobj */
	voff_t			writeoff;	/* write offset */
	vsize_t			writelen;	/* write len */
	int			refcount;	/* refcount on mapping */
	int			flags;		/* extra state */
	short		advice;		// IO_ADV_MASK masks with 0x03 so it would be safe to store it in even a i8 (flags defined in sys/uvm/uvm_extern.h)

#ifndef UBC_NO_HASH
	LIST_ENTRY(ubc_map)	hash;		/* hash table */
#endif
#ifndef __wasm__
	TAILQ_ENTRY(ubc_map)	inactive;	/* inactive queue */
	LIST_ENTRY(ubc_map)	list;		/* per-object list */
#else
	struct {
		struct ubc_map *prev;
		struct ubc_map *next;
	} inactive;
	struct ubc_map *prev;
	struct ubc_map *next;
#if UBC_MAP_HAS_PAGE_ARRAY
	struct vm_page *pages[UBC_WIN_NPAGES];
#else
	uint32_t page_bitmap;
	struct vm_page *page_head;
	struct vm_page *page_tail;
#endif
#endif
};

TAILQ_HEAD(ubc_inactive_head, ubc_map);
static struct ubc_object {
	struct uvm_object uobj;		/* glue for uvm_map() */
	char *kva;					/* where ubc_object is mapped */
	struct ubc_map *umap;		/* array of ubc_map's */

#ifndef UBC_NO_HASH
	LIST_HEAD(, ubc_map) *hash;	/* hashtable for cached ubc_map's */
	u_long hashmask;		/* mask for hashtable */
#endif

#ifndef __wasm__
	struct ubc_inactive_head *inactive;
#else
	struct {
		struct ubc_map *head;
		struct ubc_map *tail;
	} inactive;
#endif
					/* inactive queues for ubc_map's */
} ubc_object;

const struct uvm_pagerops ubc_pager = {
	.pgo_fault = ubc_fault,
	/* ... rest are NULL */
};

/* Use value at least as big as maximum page size supported by architecture */
#define UBC_MAX_WINSHIFT	\
    ((1 << UBC_WINSHIFT) > MAX_PAGE_SIZE ? UBC_WINSHIFT : ilog2(MAX_PAGE_SIZE))

int ubc_nwins = UBC_NWINS;		// 1024 (about 8mb if all in use)
const int ubc_winshift = UBC_MAX_WINSHIFT;
const int ubc_winsize = 1 << UBC_MAX_WINSHIFT;
#if defined(PMAP_PREFER)
int ubc_nqueues;
#define UBC_NQUEUES ubc_nqueues
#else
#define UBC_NQUEUES 1
#endif

#if defined(UBC_WIN_NPAGES) && (1 << UBC_WINSHIFT) != (UBC_WIN_NPAGES * PAGE_SIZE)
#error "UBC_WIN_NPAGES must match UBC_WINSHIFT"
#endif

#if defined(UBC_STATS)

#define	UBC_EVCNT_DEFINE(name) \
struct evcnt ubc_evcnt_##name = \
EVCNT_INITIALIZER(EVCNT_TYPE_MISC, NULL, "ubc", #name); \
EVCNT_ATTACH_STATIC(ubc_evcnt_##name);
#define	UBC_EVCNT_INCR(name) ubc_evcnt_##name.ev_count++

#else /* defined(UBC_STATS) */

#define	UBC_EVCNT_DEFINE(name)	/* nothing */
#define	UBC_EVCNT_INCR(name)	/* nothing */

#endif /* defined(UBC_STATS) */

UBC_EVCNT_DEFINE(wincachehit)
UBC_EVCNT_DEFINE(wincachemiss)
UBC_EVCNT_DEFINE(faultbusy)

/*
 * ubc_init
 *
 * init pager private data structures.
 */
void
ubc_init(void)
{
	struct ubc_map *umap, *umap_first;
#ifdef UBC_WIN_PAGES_PREALLOC
	struct mm_page *pg;
#endif
	/*
	 * Make sure ubc_winshift is sane.
	 */
	KASSERT(ubc_winshift >= PAGE_SHIFT);

	// TODO: ubc cache is one of those places where memory buckets would come handy, 
	// to allocate the UBC_WIN_NPAGES so we always have memory for file read/write

	/*
	 * init ubc_object.
	 * alloc and init ubc_map's.
	 * init inactive queues.
	 * alloc and init hashtable.
	 * map in ubc_object.
	 */

	uvm_obj_init(&ubc_object.uobj, &ubc_pager, true, UVM_OBJ_KERN);

	umap = kmem_zalloc(ubc_nwins * sizeof(struct ubc_map), KM_SLEEP);
	if (umap == NULL)
		panic("ubc_init: failed to allocate ubc_map");
	
	umap_first = umap;
	ubc_object.umap = umap;

#ifdef PMAP_PREFER
	PMAP_PREFER(0, &va, 0, 0);	/* kernel is never topdown */
	ubc_nqueues = va >> ubc_winshift;
	if (ubc_nqueues == 0) {
		ubc_nqueues = 1;
	}
#endif

	ubc_object.inactive.head = umap;
	ubc_object.inactive.tail = umap;
	umap++;

	for (int i = 1; i < ubc_nwins; i++) {
		ubc_object.inactive.tail->inactive.next = umap;
		umap->inactive.prev = ubc_object.inactive.tail;
		ubc_object.inactive.tail = umap;
		umap++;
	}


	ubc_dbg("%s first umap = %p last umap = %p", __func__, umap_first, umap_first + (ubc_nwins - 1));

#ifdef UBC_WIN_PAGES_PREALLOC
	umap = umap_first;
	pg = __ubc_page_bucket;
	for (int y = 0; y < ubc_nwins; y++) {
#ifdef UBC_MAP_HAS_PAGE_ARRAY
		for (int x = 0; x < UBC_WIN_NPAGES; x++) {
			pg->flags |= PG_FAKE;
			umap->pages[x] = (struct vm_page *)pg;
			pg = pg->pageq.list.li_next;
		}
		if (pg != NULL) {
			if (pg->pageq.list.li_prev != NULL) {
				pg->pageq.list.li_prev->pageq.list.li_next = NULL;
				pg->pageq.list.li_prev = NULL;
			}
		}
#else
#error "cut out segments.."
#endif
		umap++;
	}
#endif

#ifndef UBC_NO_HASH
	ubc_object.hash = hashinit(ubc_nwins, HASH_LIST, true, &ubc_object.hashmask);
	for (int i = 0; i <= ubc_object.hashmask; i++) {
		LIST_INIT(&ubc_object.hash[i]);
	}
#endif

#ifndef __WASM
	if (uvm_map(kernel_map, (vaddr_t *)&ubc_object.kva,
		    ubc_nwins << ubc_winshift, &ubc_object.uobj, 0, (vsize_t)va,
		    UVM_MAPFLAG(UVM_PROT_RW, UVM_PROT_RW, UVM_INH_NONE,
				UVM_ADV_RANDOM, UVM_FLAG_NOMERGE)) != 0) {
		panic("ubc_init: failed to map ubc_object");
	}
#endif
#ifndef UBC_NO_HASH
	hashstat_register("ubchash", ubchash_stats);
#endif
}

void
ubchist_init(void)
{

	UVMHIST_INIT(ubchist, 300);
}

/*
 * ubc_fault_page: helper of ubc_fault to handle a single page.
 *
 * => Caller has UVM object locked.
 * => Caller will perform pmap_update().
 */

static inline int
ubc_fault_page(const struct uvm_faultinfo *ufi, const struct ubc_map *umap,
    struct vm_page *pg, vm_prot_t prot, vm_prot_t access_type, vaddr_t va)
{
	vm_prot_t mask;
	int error;
	bool rdonly;

	KASSERT(rw_write_held(pg->uobject->vmobjlock));

	KASSERT((pg->flags & PG_FAKE) == 0);
	if (pg->flags & PG_RELEASED) {
		kmem_page_free((void *)pg->phys_addr, 1);
		return 0;
	}
#if 0
	if (pg->loan_count != 0) {

		/*
		 * Avoid unneeded loan break, if possible.
		 */

		if ((access_type & VM_PROT_WRITE) == 0) {
			prot &= ~VM_PROT_WRITE;
		}
		if (prot & VM_PROT_WRITE) {
			struct vm_page *newpg;

			newpg = uvm_loanbreak(pg);
			if (newpg == NULL) {
				uvm_page_unbusy(&pg, 1);
				return ENOMEM;
			}
			pg = newpg;
		}
	}
#endif

	/*
	 * Note that a page whose backing store is partially allocated
	 * is marked as PG_RDONLY.
	 *
	 * it's a responsibility of ubc_alloc's caller to allocate backing
	 * blocks before writing to the window.
	 */

	KASSERT((pg->flags & PG_RDONLY) == 0 ||
	    (access_type & VM_PROT_WRITE) == 0 ||
	    pg->offset < umap->writeoff ||
	    pg->offset + PAGE_SIZE > umap->writeoff + umap->writelen);
#if 0
	rdonly = uvm_pagereadonly_p(pg);
	mask = rdonly ? ~VM_PROT_WRITE : VM_PROT_ALL;

	error = pmap_enter(ufi->orig_map->pmap, va, VM_PAGE_TO_PHYS(pg),
	    prot & mask, PMAP_CANFAIL | (access_type & mask));
#endif
	uvm_pagelock(pg);
	uvm_pageactivate(pg);
	uvm_pagewakeup(pg);
	uvm_pageunlock(pg);
	pg->flags &= ~PG_BUSY;
	UVM_PAGE_OWN(pg, NULL);

	return error;
}

/*
 * ubc_fault: fault routine for ubc mapping
 */

static int
ubc_fault(struct uvm_faultinfo *ufi, vaddr_t ign1, struct vm_page **ign2, int ign3, int ign4, vm_prot_t access_type, int flags)
{
	struct uvm_object *uobj;
	struct ubc_map *umap;
	vaddr_t va, eva, ubc_offset, slot_offset;
	struct vm_page *pgs[howmany(ubc_winsize, MIN_PAGE_SIZE)];
	int i, error, npages;
	vm_prot_t prot;

	UVMHIST_FUNC(__func__); UVMHIST_CALLED(ubchist);

	/*
	 * no need to try with PGO_LOCKED...
	 * we don't need to have the map locked since we know that
	 * no one will mess with it until our reference is released.
	 */

	if (flags & PGO_LOCKED) {
		//uvmfault_unlockall(ufi, NULL, &ubc_object.uobj);
		flags &= ~PGO_LOCKED;
	}

	va = ufi->orig_rvaddr;
	ubc_offset = va - (vaddr_t)ubc_object.kva;
	umap = &ubc_object.umap[ubc_offset >> ubc_winshift];
	KASSERT(umap->refcount != 0);
	KASSERT((umap->flags & UMAP_PAGES_LOCKED) == 0);
	slot_offset = ubc_offset & (ubc_winsize - 1);

	/*
	 * some platforms cannot write to individual bytes atomically, so
	 * software has to do read/modify/write of larger quantities instead.
	 * this means that the access_type for "write" operations
	 * can be VM_PROT_READ, which confuses us mightily.
	 *
	 * deal with this by resetting access_type based on the info
	 * that ubc_alloc() stores for us.
	 */

	access_type = umap->writelen ? VM_PROT_WRITE : VM_PROT_READ;
	UVMHIST_LOG(ubchist, "va %#jx ubc_offset %#jx access_type %jd",
	    va, ubc_offset, access_type, 0);

	if ((access_type & VM_PROT_WRITE) != 0) {
#ifndef PRIxOFF		/* XXX */
#define PRIxOFF "jx"	/* XXX */
#endif			/* XXX */
		KASSERTMSG((trunc_page(umap->writeoff) <= slot_offset),
		    "out of range write: slot=%#"PRIxVSIZE" off=%#"PRIxOFF,
		    slot_offset, (intmax_t)umap->writeoff);
		KASSERTMSG((slot_offset < umap->writeoff + umap->writelen),
		    "out of range write: slot=%#"PRIxVADDR
		        " off=%#"PRIxOFF" len=%#"PRIxVSIZE,
		    slot_offset, (intmax_t)umap->writeoff, umap->writelen);
	}

	/* no umap locking needed since we have a ref on the umap */
	uobj = umap->uobj;

	if ((access_type & VM_PROT_WRITE) == 0) {
		npages = (ubc_winsize - slot_offset) >> PAGE_SHIFT;
	} else {
		npages = (round_page(umap->offset + umap->writeoff +
		    umap->writelen) - (umap->offset + slot_offset))
		    >> PAGE_SHIFT;
		flags |= PGO_PASTEOF;
	}

again:
	memset(pgs, 0, sizeof (pgs));
	rw_enter(uobj->vmobjlock, RW_WRITER);

	UVMHIST_LOG(ubchist, "slot_offset %#jx writeoff %#jx writelen %#jx ",
	    slot_offset, umap->writeoff, umap->writelen, 0);
	UVMHIST_LOG(ubchist, "getpages uobj %#jx offset %#jx npages %jd",
	    (uintptr_t)uobj, umap->offset + slot_offset, npages, 0);

	error = (*uobj->pgops->pgo_get)(uobj, umap->offset + slot_offset, pgs,
	    &npages, 0, access_type, umap->advice, flags | PGO_NOBLOCKALLOC |
	    PGO_NOTIMESTAMP);
	UVMHIST_LOG(ubchist, "getpages error %jd npages %jd", error, npages, 0,
	    0);

	if (error == EAGAIN) {
		kpause("ubc_fault", false, hz >> 2, NULL);
		goto again;
	}
	if (error) {
		return error;
	}

	/*
	 * For virtually-indexed, virtually-tagged caches we should avoid
	 * creating writable mappings when we do not absolutely need them,
	 * since the "compatible alias" trick does not work on such caches.
	 * Otherwise, we can always map the pages writable.
	 */

#ifdef PMAP_CACHE_VIVT
	prot = VM_PROT_READ | access_type;
#else
	prot = VM_PROT_READ | VM_PROT_WRITE;
#endif

	va = ufi->orig_rvaddr;
	eva = ufi->orig_rvaddr + (npages << PAGE_SHIFT);

	UVMHIST_LOG(ubchist, "va %#jx eva %#jx", va, eva, 0, 0);

	/*
	 * Note: normally all returned pages would have the same UVM object.
	 * However, layered file-systems and e.g. tmpfs, may return pages
	 * which belong to underlying UVM object.  In such case, lock is
	 * shared amongst the objects.
	 */
	rw_enter(uobj->vmobjlock, RW_WRITER);
	for (i = 0; va < eva; i++, va += PAGE_SIZE) {
		struct vm_page *pg;

		UVMHIST_LOG(ubchist, "pgs[%jd] = %#jx", i, (uintptr_t)pgs[i],
		    0, 0);
		pg = pgs[i];

		if (pg == NULL || pg == PGO_DONTCARE) {
			continue;
		}
		KASSERT(uobj->vmobjlock == pg->uobject->vmobjlock);
		error = ubc_fault_page(ufi, umap, pg, prot, access_type, va);
		if (error) {
			/*
			 * Flush (there might be pages entered), drop the lock,
			 * and perform uvm_wait().  Note: page will re-fault.
			 */
			//pmap_update(ufi->orig_map->pmap);
			rw_exit(uobj->vmobjlock);
			uvm_wait("ubc_fault");
			rw_enter(uobj->vmobjlock, RW_WRITER);
		}
	}
	/* Must make VA visible before the unlock. */
	//pmap_update(ufi->orig_map->pmap);
	rw_exit(uobj->vmobjlock);

	return 0;
}

#ifdef UBC_MAP_DEBUG
static void __noinline
umap_dump_all(void)
{
	struct ubc_map *umap;
	bool did_abort = false;
	int guard = 0;
	printf("------- dumping ubc_object.inactive from head = %p to tail = %p------- \n", ubc_object.inactive.head, ubc_object.inactive.tail);
	for (umap = ubc_object.inactive.head; umap != NULL; umap = umap->inactive.next) {
		printf("\taddr = %p uobj = %p offset = %lld list {prev = %p next = %p} inactive {prev = %p next = %p}\n", umap, umap->uobj, umap->offset, umap->prev, umap->next, umap->inactive.prev, umap->inactive.next);
		if (guard > ubc_nwins) {
			printf("------- end of dump ------- \n");
			printf("abort: inactive list contains more links than file mapping windows\n");
			did_abort = true;
		}
		guard++;
	}

	if (!did_abort) {
		printf("------- end of dump ------- \n");
		did_abort = true;
	}

	if (did_abort) {
		umap = ubc_object.umap;
		printf("------- dumping from ubc_object.umap (%p)------- \n", ubc_object.umap);
		for (int i = 0; i < ubc_nwins; i++) {
			printf("\taddr = %p uobj = %p offset = %lld list {prev = %p next = %p} inactive {prev = %p next = %p}\n", umap, umap->uobj, umap->offset, umap->prev, umap->next, umap->inactive.prev, umap->inactive.next);
			umap++;
		}

		printf("------- end of dump ------- \n");		
	} else {
		printf("------- end of dump ------- \n");
	}

}
#endif

/*
 * local functions
 */

static struct ubc_map * __noinline
ubc_find_mapping(struct uvm_object *uobj, voff_t offset)
{
	struct ubc_map *umap;
	int guard = 0;
#ifndef UBC_NO_HASH
	LIST_FOREACH(umap, &ubc_object.hash[UBC_HASH(uobj, offset)], hash) {
		if (umap->uobj == uobj && umap->offset == offset) {
			return umap;
		}
	}
	return NULL;
#else

	for (umap = uobj->uo_ubc.lh_first; umap != NULL; umap = umap->next) {
		if (umap->uobj == uobj && offset == umap->offset) {
			return umap;
		}
		guard++;
		if (guard > 10000) {
			ubc_dbg("run way too long in %s for uobj = %p first-umap %p\n", __func__, uobj, uobj->uo_ubc.lh_first);
			umap_dump_all();
			__panic_abort();
		}
	}

	return NULL;
#endif
}

#ifndef UBC_MAP_HAS_PAGE_ARRAY
static struct vm_page * __noinline
ubc_find_page_at(struct ubc_map *umap, voff_t offset)
{
	struct vm_page *pg;
	voff_t off;

	for (pg = umap->page_head; pg != NULL; pg = pg->pageq.list.li_next) {
		off = pg->offset;
		if (off >= offset && offset < off + 4096) {
			return pg;
		}
	}

	return NULL;
}
#endif

// pushes to tail, expects vmobjlock to be held
static void __noinline
ubc_object_attach_inactive_tail(struct ubc_map *umap)
{
	struct ubc_map *prev;
	prev = ubc_object.inactive.tail;
	if (prev != NULL) {
		ubc_object.inactive.tail = umap;
		umap->inactive.prev = prev;
		prev->inactive.next = umap;
	} else {
		ubc_object.inactive.head = umap;
		ubc_object.inactive.tail = umap;
	}

	ubc_dbg("%s ubc_object.inactive.head %p ubc_object.inactive.tail %p", __func__, ubc_object.inactive.head, ubc_object.inactive.tail);

	return;
}

// pushes to tail, expects vmobjlock to be held
static void __noinline
ubc_object_attach_inactive_head(struct ubc_map *umap)
{
	struct ubc_map *next;
	next = ubc_object.inactive.head;

	if (next != NULL) {
		ubc_object.inactive.head = umap;
		umap->inactive.next = next;
		next->inactive.prev = umap;
	} else {
		ubc_object.inactive.head = umap;
		ubc_object.inactive.tail = umap;
	}

	ubc_dbg("%s ubc_object.inactive.head %p ubc_object.inactive.tail %p", __func__, ubc_object.inactive.head, ubc_object.inactive.tail);
}

static __noinline void
ubc_object_detach_inactive(struct ubc_map *umap)
{
	if (umap->inactive.next != NULL)
		umap->inactive.next->inactive.prev = umap->inactive.prev;

	if (umap->inactive.prev != NULL)
		umap->inactive.prev->inactive.next = umap->inactive.next;

	if (umap == ubc_object.inactive.head)
		ubc_object.inactive.head = umap->inactive.next;

	if (umap == ubc_object.inactive.tail)
		ubc_object.inactive.tail = umap->inactive.prev;

	umap->inactive.prev = NULL;
	umap->inactive.next = NULL;

	ubc_dbg("%s ubc_object.inactive.head %p ubc_object.inactive.tail %p", __func__, ubc_object.inactive.head, ubc_object.inactive.tail);
}

// caller is responsible to set umap->uobj
static inline void
ubc_object_append_umap(struct uvm_object *uobj, struct ubc_map *umap)
{
	if (uobj->uo_ubc.lh_first == umap)
		return; // no work todo
	
	// first detach self if needed.
	if (umap->prev != NULL)
		umap->prev->next = umap->next;

	if (umap->next != NULL)
		umap->next->prev = umap->prev;

	umap->next = uobj->uo_ubc.lh_first;
	if (umap->next != NULL)
		umap->next->prev = umap;
	uobj->uo_ubc.lh_first = umap;
	umap->prev = NULL;
}

// caller is responsible to unset umap->uobj
static __noinline void
ubc_object_remove_umap(struct uvm_object *uobj, struct ubc_map *umap)
{
	if (uobj->uo_ubc.lh_first == umap)
		uobj->uo_ubc.lh_first = umap->next;

	if (umap->prev != NULL)
		umap->prev->next = umap->next;

	if (umap->next != NULL)
		umap->next->prev = umap->prev;

	umap->prev = NULL;
	umap->next = NULL;
}

// pops from head, expects vmobjlock to be held
static struct ubc_map * __noinline
ubc_object_pop_umap(void)
{
	struct uvm_object *uobj;
	struct ubc_map *umap, *next;

	ubc_dbg("%s at-enter ubc_object.inactive.head %p ubc_object.inactive.tail %p", __func__, ubc_object.inactive.head, ubc_object.inactive.tail);

	umap = ubc_object.inactive.head;
	if (umap == NULL)
		return umap;
	
	if (umap == ubc_object.inactive.tail) {
		ubc_object.inactive.tail = NULL;
	}

	if (umap->uobj) {
		// unmap any cached mapping
		uobj = umap->uobj;
		rw_enter(uobj->vmobjlock, RW_WRITER);
		
		ubc_object_remove_umap(uobj, umap);
		umap->uobj = NULL;

		rw_exit(uobj->vmobjlock);

		if (umap->flags & UMAP_MAPPING_CACHED)
			umap->flags &= ~(UMAP_MAPPING_CACHED);

		umap->prev = NULL;
		umap->next = NULL;
	}

	next = umap->inactive.next;
	if (next != NULL) {
		next->inactive.prev = NULL;
	}

	ubc_object.inactive.head = next;
	umap->inactive.next = NULL;
	umap->inactive.prev = NULL;	// prev should already be null, but the above removal of mapping could be done using trylock
	umap->flags &= ~(UMAP_INACTIVE_QUEUE);

	ubc_dbg("%s at-exit ubc_object.inactive.head %p ubc_object.inactive.tail %p", __func__, ubc_object.inactive.head, ubc_object.inactive.tail);

	return umap;
}

/*
 * ubc interface functions
 */

/*
 * ubc_alloc:  allocate a file mapping window
 */
static struct ubc_map * __noinline
ubc_alloc(struct uvm_object *uobj, voff_t offset, vsize_t *lenp, int advice,
    int flags, struct vm_page **pgs, int *npagesp)
{
	struct vm_page *pg;
	struct vm_page *pgo_pgs[UBC_WIN_NPAGES];
	struct ubc_map *umap;
	voff_t umap_offset;
	voff_t end_offset, pgo_off;
	uint32_t page_index;
	int pgo_npages, pgidx;
	int error;
	bool new_map;

	UVMHIST_FUNC(__func__);
	UVMHIST_CALLARGS(ubchist, "uobj %#jx offset %#jx len %#jx", (uintptr_t)uobj, offset, *lenp, 0);

	KASSERT(*lenp > 0);
	umap_offset = (offset & ~((voff_t)ubc_winsize - 1));
	page_index = ((offset - umap_offset) >> PAGE_SHIFT);
	end_offset = MIN((offset + *lenp) , umap_offset + ubc_winsize);
	*lenp = (end_offset - offset);
	new_map = false;

	KASSERT(*lenp > 0);

	rw_enter(ubc_object.uobj.vmobjlock, RW_WRITER);
again:
	/*
	 * The UVM object is already referenced.
	 * Lock order: UBC object -> ubc_map::uobj.
	 */
	umap = ubc_find_mapping(uobj, umap_offset);
	if (umap == NULL) {
		struct uvm_object *oobj;

		UBC_EVCNT_INCR(wincachemiss);
		ubc_dbg("%s ubc-win cache miss uobc = %p umap_offset = %lld offset = %lld length = %lu", __func__, uobj, umap_offset, offset, *lenp);

		new_map = true;
		umap = ubc_object_pop_umap();
		if (umap == NULL) {
			ubc_dbg("%s pop inactive returned NULL", __func__);
			rw_exit(ubc_object.uobj.vmobjlock);
			kpause("ubc_alloc", false, hz >> 2, NULL);
			rw_enter(ubc_object.uobj.vmobjlock, RW_WRITER);
			goto again;
		}
		// TODO: this expects file data to be mapped to virtual-memory which will not work for wasm.
		
#if 0
		// Remove from old hash (if any), add to new hash.
		oobj = umap->uobj;
		if (oobj != NULL) {
			/*
			 * Mapping must be removed before the list entry,
			 * since there is a race with ubc_purge().
			 */
			if (umap->flags & UMAP_MAPPING_CACHED) {
				umap->flags &= ~UMAP_MAPPING_CACHED;
#ifndef __wasm__
				rw_enter(oobj->vmobjlock, RW_WRITER);
				pmap_remove(pmap_kernel(), va, va + ubc_winsize);
				pmap_update(pmap_kernel());
				rw_exit(oobj->vmobjlock);
#endif
			}
#ifndef UBC_NO_HASH
			LIST_REMOVE(umap, hash);
#endif
			LIST_REMOVE(umap, list);
		} else {
			KASSERT((umap->flags & UMAP_MAPPING_CACHED) == 0);
		}
#endif
		umap->uobj = uobj;
		umap->offset = umap_offset;
#ifndef UBC_NO_HASH
		LIST_INSERT_HEAD(&ubc_object.hash[UBC_HASH(uobj, umap_offset)],
		    umap, hash);
#endif
#ifdef UBC_MAP_HAS_PAGE_ARRAY
		// zero-pages
		pgo_off = umap_offset;
		for (int i = 0; i < UBC_WIN_NPAGES; i++) {
			pg = umap->pages[i];
			if (pg != NULL) {
				wasm_memory_fill((void *)pg->phys_addr, 0, PAGE_SIZE);
				pg->flags |= PG_FAKE;
				pg->offset = pgo_off;
				pg->uobject = uobj;
			}
			pgo_off += PAGE_SIZE;
		}
#else
		// zero-pages
		for (pg = umap->page_head; pg != NULL; pg = pg->pageq.list.li_next) {
			wasm_memory_fill((void *)pg->phys_addr, 0, PAGE_SIZE);
			pg->flags |= PG_FAKE;
		}
#endif

		ubc_object_append_umap(uobj, umap);
	} else {
		UBC_EVCNT_INCR(wincachehit);
		ubc_dbg("%s ubc-win cache hit uobc = %p umap = %p umap_offset = %lld offset = %lld length = %lu", __func__, uobj, umap, umap_offset, offset, *lenp);
	}

	// removes the mapping from the in-active queue (only if not freshly got)
	if (umap->refcount == 0 && (umap->flags & UMAP_INACTIVE_QUEUE) != 0) {
		ubc_object_detach_inactive(umap);
		umap->flags &= ~(UMAP_INACTIVE_QUEUE);
	}

	if (flags & UBC_WRITE) {
		KASSERTMSG(umap->writeoff == 0, "ubc_alloc: concurrent writes to uobj %p", uobj);
		KASSERTMSG(umap->writelen == 0, "ubc_alloc: concurrent writes to uobj %p", uobj);
		umap->writeoff = umap_offset;
		umap->writelen = *lenp;
	}

	umap->refcount++;
	umap->advice = (int16_t)advice;
	rw_exit(ubc_object.uobj.vmobjlock);
	UVMHIST_LOG(ubchist, "umap %#jx refs %jd va %#jx flags %#jx", (uintptr_t)umap, umap->refcount, (uintptr_t)va, flags);

	// zero-fill the page array caller has given.
	memset(pgs, 0, *(npagesp) * sizeof(void *));
	memset(pgo_pgs, 0, sizeof(pgo_pgs));

#ifdef UBC_MAP_HAS_PAGE_ARRAY
	ubc_dbg("%s page_index = %d", __func__, page_index);
	// check
	pgidx = 0;
	for (int i = page_index; i < UBC_WIN_NPAGES; i++) {
		pg = umap->pages[i];
#ifndef UBC_WIN_PAGES_PREALLOC
		if (pg == NULL) {
			pg = kmem_page_alloc(1, 0);
		}
#endif
		pgo_off = pg->offset;
		if ((pg->flags & PG_FAKE) != 0) {
			pgo_pgs[0] = pg;
			pgo_npages = 1;
			int gpflags = PGO_SYNCIO/*|PGO_OVERWRITE*/|PGO_PASTEOF|PGO_NOBLOCKALLOC|PGO_NOTIMESTAMP;
			pg->flags |= PG_BYPASS_FILE_MAP;	// TODO: change implementation in genfs_io so that if pages are given to pgo_get its explicity mapped to that page
			error = (*uobj->pgops->pgo_get)(uobj, pg->offset, pgo_pgs, &pgo_npages, 0, VM_PROT_READ | VM_PROT_WRITE, advice, gpflags);
			pg->flags &= ~(PG_BYPASS_FILE_MAP);
			if (error != 0) {
				ubc_dbg("%s got error %d from pgo_get() into umap->pages[%d] at pg-addr %p phys-addr = %p pg->offset %lld\n", __func__, error, i, pg, (void *)pg->phys_addr, pg->offset);
				goto out;
			} else {
				ubc_dbg("%s successful read into umap->pages[%d] at pg-addr %p phys-addr = %p pg->offset %lld\n", __func__, i, pg, (void *)pg->phys_addr, pg->offset);
				pg->flags &= ~(PG_FAKE);
			}
		}

		pgs[pgidx] = pg;
		pgidx++;
		if ((pgo_off + PAGE_SIZE) >= end_offset) {
			break;
		}
	}

	if (npagesp)
		*npagesp = pgidx;
	
	umap->flags |= UMAP_PAGES_LOCKED;

	return umap;
#else
#error "IMPLEMENT THIS without ptr array"
#endif
#if 0
	if (flags & UBC_FAULTBUSY) {
		int npages = (*lenp + (offset & (PAGE_SIZE - 1)) + PAGE_SIZE - 1) >> PAGE_SHIFT;
		int gpflags = PGO_SYNCIO/*|PGO_OVERWRITE*/|PGO_PASTEOF|PGO_NOBLOCKALLOC|PGO_NOTIMESTAMP; // TODO: wasm
		int i;
		KDASSERT(flags & UBC_WRITE);
		KASSERT(npages <= *npagesp);
		KASSERT(umap->refcount == 1);

		UBC_EVCNT_INCR(faultbusy);
again_faultbusy:
		rw_enter(uobj->vmobjlock, RW_WRITER);
		if (umap->flags & UMAP_MAPPING_CACHED) {
			umap->flags &= ~UMAP_MAPPING_CACHED;
#ifndef __wasm__
			pmap_remove(pmap_kernel(), va, va + ubc_winsize);
#endif
		}
		memset(pgs, 0, *npagesp * sizeof(void *));

		error = (*uobj->pgops->pgo_get)(uobj, trunc_page(offset), pgs, &npages, 0, VM_PROT_READ | VM_PROT_WRITE, advice, gpflags);
		UVMHIST_LOG(ubchist, "faultbusy getpages %jd", error, 0, 0, 0);
		if (error) {
			// Flush: the mapping above might have been removed.
			//pmap_update(pmap_kernel());
			goto out;
		}
#ifdef UBC_MAP_HAS_PAGE_ARRAY

#else
		for (i = 0; i < npages; i++) {
			struct vm_page *pg = pgs[i];

			KASSERT(pg->uobject == uobj);
			if (pg == NULL) {
				//rw_exit(uobj->vmobjlock);
				goto again_faultbusy;
			}

			//rw_exit(uobj->vmobjlock);


			pgs[i] = pg;
			if (umap->pg_head == NULL) {
				umap->pg_head = pg;
				umap->pg_tail = pg;
			} else {
				if (umap->pg_tail)
					umap->pg_tail->pageq.list.li_next = pg;
				umap->pg_tail = pg;
			}
			ubc_dbg("%s pg[%d] is %p phys_addr %p\n", __func__, i, pg, (void *)pg->phys_addr);
#if 0
			if (pg->loan_count != 0) {
				rw_enter(uobj->vmobjlock, RW_WRITER);
				if (pg->loan_count != 0) {
					pg = uvm_loanbreak(pg);
				}
				if (pg == NULL) {
					pmap_kremove(va, ubc_winsize);
					pmap_update(pmap_kernel());
					uvm_page_unbusy(pgs, npages);
					rw_exit(uobj->vmobjlock);
					uvm_wait("ubc_alloc");
					goto again_faultbusy;
				}
				rw_exit(uobj->vmobjlock);
				pgs[i] = pg;
			}
			pmap_kenter_pa(
			    va + trunc_page(slot_offset) + (i << PAGE_SHIFT),
			    VM_PAGE_TO_PHYS(pg),
			    VM_PROT_READ | VM_PROT_WRITE, 0);
#endif
		}
#endif
		//pmap_update(pmap_kernel());
		umap->flags |= UMAP_PAGES_LOCKED;
		*npagesp = npages;
	} else {
		KASSERT((umap->flags & UMAP_PAGES_LOCKED) == 0);
	}
#endif
out:
	return umap;
}

/*
 * ubc_release: free a file mapping window.
 */
static void __noinline
ubc_release(struct ubc_map *umap, int flags, struct vm_page **pgs, int npages)
{
	struct uvm_object *uobj;
	bool unmapped;
	UVMHIST_FUNC(__func__);
	UVMHIST_CALLARGS(ubchist, "va %#jx", (uintptr_t)va, 0, 0, 0);

	uobj = umap->uobj;
	KASSERT(uobj != NULL);

	if (umap->flags & UMAP_PAGES_LOCKED) {
#if 0
		const voff_t endoff = umap->writeoff + umap->writelen;
		const voff_t zerolen = round_page(endoff) - endoff;

		KASSERT(npages == (round_page(endoff) - trunc_page(umap->writeoff)) >> PAGE_SHIFT);
		KASSERT((umap->flags & UMAP_MAPPING_CACHED) == 0);
		if (zerolen) {
			memset((char *)umapva + endoff, 0, zerolen);
		}
#endif
		umap->flags &= ~UMAP_PAGES_LOCKED;
		rw_enter(uobj->vmobjlock, RW_WRITER);
		for (u_int i = 0; i < npages; i++) {
			struct vm_page *pg = pgs[i];
#ifdef DIAGNOSTIC
			paddr_t pa;
			bool rv;
			rv = pmap_extract(pmap_kernel(), umapva +
			    umap->writeoff + (i << PAGE_SHIFT), &pa);
			KASSERT(rv);
			KASSERT(PHYS_TO_VM_PAGE(pa) == pg);
#endif
			pg->flags &= ~PG_FAKE;
			KASSERTMSG(uvm_pagegetdirty((struct mm_page *)pg) == UVM_PAGE_STATUS_DIRTY, "page %p not dirty", pg);
			//KASSERT(pg->loan_count == 0);
#if 0
			if (uvmpdpol_pageactivate_p(pg)) {
				uvm_pagelock(pg);
				uvm_pageactivate(pg);
				uvm_pageunlock(pg);
			}
#endif
		}
#if 0
		pmap_kremove(umapva, ubc_winsize);
		pmap_update(pmap_kernel());
#endif
		uvm_page_unbusy(pgs, npages);
		rw_exit(uobj->vmobjlock);
		unmapped = true;
	} else {
		unmapped = false;
	}

	rw_enter(ubc_object.uobj.vmobjlock, RW_WRITER);
	umap->writeoff = 0;
	umap->writelen = 0;
	umap->refcount--;
	if (umap->refcount == 0) {
		if (flags & UBC_UNMAP) {
			/*
			 * Invalidate any cached mappings if requested.
			 * This is typically used to avoid leaving
			 * incompatible cache aliases around indefinitely.
			 */
#ifndef __WASM
			rw_enter(uobj->vmobjlock, RW_WRITER);
			pmap_remove(pmap_kernel(), umapva, umapva + ubc_winsize);
			pmap_update(pmap_kernel());
			rw_exit(uobj->vmobjlock);
#endif

			umap->flags &= ~UMAP_MAPPING_CACHED;
#ifndef UBC_NO_HASH
			LIST_REMOVE(umap, hash);
#endif
			// removing from listing in vnode
			ubc_object_remove_umap(umap->uobj, umap);
			umap->uobj = NULL;

			ubc_dbg("%s mapping inactive %p into head\n", __func__, umap);
			
			ubc_object_attach_inactive_head(umap);
			
		} else {
			if (!unmapped) {
				umap->flags |= UMAP_MAPPING_CACHED;
			}
			ubc_dbg("%s mapping inactive %p into tail\n", __func__, umap);
			// ubc that are not unmapped, are inserted at tail which might allow for a future cache hit.
			ubc_object_attach_inactive_tail(umap);
			umap->flags |= UMAP_INACTIVE_QUEUE;
		}
	}
	UVMHIST_LOG(ubchist, "umap %#jx refs %jd", (uintptr_t)umap, umap->refcount, 0, 0);
	rw_exit(ubc_object.uobj.vmobjlock);

}

#ifdef __WASM
/**
 * Bypasses any file mapping cache and does read/write directly from/to 1x page in uio iov. This is used for 
 * loading executable files from disk on the wasm platform, since that mapping we do not want to cache anyways.
 */
int
ubc_uiomove_direct_wasm(struct uvm_object *uobj, struct uio *uio, vsize_t todo, int advice, int flags)
{
	struct mm_page *pg;
	int error, gpflags;
	int npgs = 1;
	struct vm_page *pgs[1];
	

	gpflags = PGO_SYNCIO/*|PGO_OVERWRITE*/|PGO_PASTEOF|PGO_NOBLOCKALLOC| PGO_NOTIMESTAMP;
	error = 0;

	if (uio->uio_rw == UIO_READ) {
		pg = paddr_to_page(uio->uio_iov->iov_base);
		pgs[0] = (struct vm_page *)pg;

		//rw_enter(uobj->vmobjlock, RW_WRITER);
		error = (uobj->pgops->pgo_get)(uobj, uio->uio_offset, pgs, &npgs, 0, VM_PROT_READ | VM_PROT_WRITE, advice, flags);
		uio->uio_resid = 0;
		ubc_dbg("call to uobj->pgops->pgo_get returned %d\n", error);
		//rw_exit(uobj->vmobjlock);
	} else if (uio->uio_rw == UIO_WRITE) {
		/*
		rw_enter(uobj->vmobjlock, RW_WRITER);
		(uobj->pgops->pgo_put)(uobj, uio->uio_offset);
		rw_exit(uobj->vmobjlock);
		*/
	}

	return error;
}
#endif

/*
 * ubc_uiomove: move data to/from an object.
 */
//#ifndef __WASM
int
ubc_uiomove(struct uvm_object *uobj, struct uio *uio, vsize_t todo, int advice, int flags)
{
	const bool overwrite = (flags & UBC_FAULTBUSY) != 0;
	struct vm_page *pgs[UBC_WIN_NPAGES];
#ifdef __wasm__
	struct mm_page *pg;
	struct ubc_map *ubc_win;
	void *pg_win;
	uint32_t pg_winsz;
	voff_t win_off;
#endif
	voff_t off;
	int error, npages;

	KASSERT(todo <= uio->uio_resid);
	KASSERT(((flags & UBC_WRITE) != 0 && uio->uio_rw == UIO_WRITE) ||
	    ((flags & UBC_READ) != 0 && uio->uio_rw == UIO_READ));

#ifdef UBC_USE_PMAP_DIRECT
	/*
	 * during direct access pages need to be held busy to prevent them
	 * changing identity, and therefore if we read or write an object
	 * into a mapped view of same we could deadlock while faulting.
	 *
	 * avoid the problem by disallowing direct access if the object
	 * might be visible somewhere via mmap().
	 *
	 * XXX concurrent reads cause thundering herd issues with PG_BUSY.
	 * In the future enable by default for writes or if ncpu<=2, and
	 * make the toggle override that.
	 */
	if ((ubc_direct && (flags & UBC_ISMAPPED) == 0) ||
	    (flags & UBC_FAULTBUSY) != 0) {
		return ubc_uiomove_direct(uobj, uio, todo, advice, flags);
	}
#endif

#ifdef __WASM
	if (uio->uio_iovcnt == 1 && uio->uio_iov->iov_len == PAGE_SIZE && ((uintptr_t)(uio->uio_iov->iov_base) % PAGE_SIZE) == 0) {
		pg = paddr_to_page(uio->uio_iov->iov_base);
		if ((pg->flags & PG_BYPASS_FILE_MAP) != 0) {
			vsize_t bytelen = todo;
			error = ubc_uiomove_direct_wasm(uobj, uio, bytelen, advice, flags);
			
			return error;
		}
	}
#endif

 

	off = uio->uio_offset;
	error = 0;
	while (todo > 0) {
		vsize_t bytelen = todo;
		void *win;

		npages = UBC_WIN_NPAGES;
#ifdef __WASM
        if ((flags & UBC_FAULTBUSY) == 0) {
            flags |= UBC_FAULTBUSY;
        }
#endif
		ubc_win = ubc_alloc(uobj, off, &bytelen, advice, flags, pgs, &npages);
		if (error == 0) {
#if 0
			if (npages == 1) {
				win = (void *)(pgs[0]->phys_addr);
			} else {
				ubc_dbg("%s multi-page uiomove() not supported\n", __func__);
				__panic_abort();
			}
#endif
			win_off = off;
			pg = (struct mm_page *)pgs[0];
			for (int i = 0; i < npages; i++) {
				pg = (struct mm_page *)pgs[i];
				ubc_dbg("%s page = %p page[%d] = {uobject = %p offset = %lld}", __func__, pg, i, pg->uobject, pg->offset);
				pg_winsz = (uint32_t)(win_off - pg->offset);
				pg_win = (void *)(pg->phys_addr) + pg_winsz;
				pg_winsz = PAGE_SIZE - pg_winsz;
				pg_winsz = MIN(bytelen, pg_winsz);
				uiomove(pg_win, pg_winsz, uio);
				win_off += pg_winsz;
			}
#if 0
			error = uiomove(win, bytelen, uio);
#endif
		}
		ubc_dbg("%s file-mapping umap = %p\n", __func__, ubc_win);
		if (error != 0 && overwrite) {
			/*
			 * if we haven't initialized the pages yet,
			 * do it now.  it's safe to use memset here
			 * because we just mapped the pages above.
			 */
#ifndef __wasm__
			memset(win, 0, bytelen);
#else
			win_off = off;
			for (int i = 0; i < npages; i++) {
				pg = (struct mm_page *)pgs[i];
				ubc_dbg("%s zero-filling page = %p page[%d] = {uobject = %p offset = %lld}", __func__, pg, i, pg->uobject, pg->offset);
				pg_winsz = (uint32_t)(win_off - pg->offset);
				pg_win = (void *)(pg->phys_addr) + pg_winsz;
				pg_winsz = PAGE_SIZE - pg_winsz;
				pg_winsz = MIN(bytelen, pg_winsz);
				memset(pg_win, 0, pg_winsz);
				win_off += pg_winsz;
			}
#endif
		}
		ubc_release(ubc_win, flags, pgs, npages);
		off += bytelen;
		todo -= bytelen;
		if (error != 0 && (flags & UBC_PARTIALOK) != 0) {
			break;
		}
	}

	return error;
}
//#endif

/*
 * ubc_zerorange: set a range of bytes in an object to zero.
 */
void
ubc_zerorange(struct uvm_object *uobj, off_t off, size_t len, int flags)
{
	struct ubc_map *umap;
	struct vm_page *pgs[howmany(ubc_winsize, MIN_PAGE_SIZE)];
	struct mm_page *pg;
	void *pg_win;
	voff_t win_off;
	uint32_t pg_winsz;
	int npages;

#ifdef UBC_USE_PMAP_DIRECT
	if (ubc_direct || (flags & UBC_FAULTBUSY) != 0) {
		ubc_zerorange_direct(uobj, off, len, flags);
		return;
	}
#endif

	/*
	 * XXXUBC invent kzero() and use it
	 */

	while (len) {
		void *win;
		vsize_t bytelen = len;

		npages = __arraycount(pgs);
		umap = ubc_alloc(uobj, off, &bytelen, UVM_ADV_NORMAL, UBC_WRITE, pgs, &npages);

		win_off = off;
		for (int i = 0; i < npages; i++) {
			pg = (struct mm_page *)pgs[i];
#if __WASM_KERN_DEBUG_BIO
			printf("%s page = %p page[%d] = {uobject = %p offset = %lld}", __func__, pg, i, pg->uobject, pg->offset);
#endif
			pg_winsz = (uint32_t)(win_off - pg->offset);
			pg_win = (void *)(pg->phys_addr) + pg_winsz;
			pg_winsz = PAGE_SIZE - pg_winsz;
			pg_winsz = MIN(bytelen, pg_winsz);
			wasm_memory_fill(pg_win, 0, pg_winsz);
			win_off += pg_winsz;
		}

		ubc_release(umap, flags, pgs, npages);

		off += bytelen;
		len -= bytelen;
	}
}

#ifdef UBC_USE_PMAP_DIRECT
/* Copy data using direct map */

/*
 * ubc_alloc_direct:  allocate a file mapping window using direct map
 */
static int __noinline
ubc_alloc_direct(struct uvm_object *uobj, voff_t offset, vsize_t *lenp,
    int advice, int flags, struct vm_page **pgs, int *npages)
{
	voff_t pgoff;
	int error;
	int gpflags = flags | PGO_NOTIMESTAMP | PGO_SYNCIO;
	int access_type = VM_PROT_READ;
	UVMHIST_FUNC(__func__); UVMHIST_CALLED(ubchist);

	if (flags & UBC_WRITE) {
		if (flags & UBC_FAULTBUSY)
			gpflags |= PGO_OVERWRITE | PGO_NOBLOCKALLOC;
#if 0
		KASSERT(!UVM_OBJ_NEEDS_WRITEFAULT(uobj));
#endif

		/*
		 * Tell genfs_getpages() we already have the journal lock,
		 * allow allocation past current EOF.
		 */
		gpflags |= PGO_JOURNALLOCKED | PGO_PASTEOF;
		access_type |= VM_PROT_WRITE;
	} else {
		/* Don't need the empty blocks allocated, PG_RDONLY is okay */
		gpflags |= PGO_NOBLOCKALLOC;
	}

	pgoff = (offset & PAGE_MASK);
	*lenp = MIN(*lenp, ubc_winsize - pgoff);

again:
	*npages = (*lenp + pgoff + PAGE_SIZE - 1) >> PAGE_SHIFT;
	KASSERT((*npages * PAGE_SIZE) <= ubc_winsize);
	KASSERT(*lenp + pgoff <= ubc_winsize);
	memset(pgs, 0, *npages * sizeof(pgs[0]));

	rw_enter(uobj->vmobjlock, RW_WRITER);
	error = (*uobj->pgops->pgo_get)(uobj, trunc_page(offset), pgs,
	    npages, 0, access_type, advice, gpflags);
	UVMHIST_LOG(ubchist, "alloc_direct getpages %jd", error, 0, 0, 0);
	if (error) {
		if (error == EAGAIN) {
			kpause("ubc_alloc_directg", false, hz >> 2, NULL);
			goto again;
		}
		return error;
	}

	rw_enter(uobj->vmobjlock, RW_WRITER);
	for (int i = 0; i < *npages; i++) {
		struct vm_page *pg = pgs[i];

		KASSERT(pg != NULL);
		KASSERT(pg != PGO_DONTCARE);
		KASSERT((pg->flags & PG_FAKE) == 0 || (gpflags & PGO_OVERWRITE));
		KASSERT(pg->uobject->vmobjlock == uobj->vmobjlock);

		/* Avoid breaking loan if possible, only do it on write */
		if ((flags & UBC_WRITE) && pg->loan_count != 0) {
			pg = uvm_loanbreak(pg);
			if (pg == NULL) {
				uvm_page_unbusy(pgs, *npages);
				rw_exit(uobj->vmobjlock);
				uvm_wait("ubc_alloc_directl");
				goto again;
			}
			pgs[i] = pg;
		}

		/* Page must be writable by now */
		KASSERT((pg->flags & PG_RDONLY) == 0 || (flags & UBC_WRITE) == 0);

		/*
		 * XXX For aobj pages.  No managed mapping - mark the page
		 * dirty.
		 */
		if ((flags & UBC_WRITE) != 0) {
			uvm_pagemarkdirty(pg, UVM_PAGE_STATUS_DIRTY);
		}
	}
	rw_exit(uobj->vmobjlock);

	return 0;
}

static void __noinline
ubc_direct_release(struct uvm_object *uobj,
	int flags, struct vm_page **pgs, int npages)
{
	rw_enter(uobj->vmobjlock, RW_WRITER);
	for (int i = 0; i < npages; i++) {
		struct vm_page *pg = pgs[i];

		pg->flags &= ~PG_BUSY;
		UVM_PAGE_OWN(pg, NULL);
		if (pg->flags & PG_RELEASED) {
			pg->flags &= ~PG_RELEASED;
			kmem_page_free(pg->phys_addr, 1);
			continue;
		}

		if (uvm_pagewanted_p(pg) || uvmpdpol_pageactivate_p(pg)) {
			uvm_pagelock(pg);
			uvm_pageactivate(pg);
			uvm_pagewakeup(pg);
			uvm_pageunlock(pg);
		}

		/* Page was changed, no longer fake and neither clean. */
		if (flags & UBC_WRITE) {
			KASSERTMSG(uvm_pagegetdirty(pg) ==
			    UVM_PAGE_STATUS_DIRTY,
			    "page %p not dirty", pg);
			pg->flags &= ~PG_FAKE;
		}
	}
	rw_exit(uobj->vmobjlock);
}

static int
ubc_uiomove_process(void *win, size_t len, void *arg)
{
	struct uio *uio = (struct uio *)arg;

	return uiomove(win, len, uio);
}

static int
ubc_zerorange_process(void *win, size_t len, void *arg)
{
	memset(win, 0, len);
	return 0;
}

static int __noinline
ubc_uiomove_direct(struct uvm_object *uobj, struct uio *uio, vsize_t todo, int advice,
    int flags)
{
	const bool overwrite = (flags & UBC_FAULTBUSY) != 0;
	voff_t off;
	int error, npages;
	struct vm_page *pgs[howmany(ubc_winsize, MIN_PAGE_SIZE)];

	KASSERT(todo <= uio->uio_resid);
	KASSERT(((flags & UBC_WRITE) != 0 && uio->uio_rw == UIO_WRITE) ||
	    ((flags & UBC_READ) != 0 && uio->uio_rw == UIO_READ));

	off = uio->uio_offset;
	error = 0;
	while (todo > 0) {
		vsize_t bytelen = todo;

		error = ubc_alloc_direct(uobj, off, &bytelen, advice, flags,
		    pgs, &npages);
		if (error != 0) {
			/* can't do anything, failed to get the pages */
			break;
		}

		if (error == 0) {
			error = uvm_direct_process(pgs, npages, off, bytelen,
			    ubc_uiomove_process, uio);
		}

		if (overwrite) {
			voff_t endoff;

			/*
			 * if we haven't initialized the pages yet due to an
			 * error above, do it now.
			 */
			if (error != 0) {
				(void) uvm_direct_process(pgs, npages, off,
				    bytelen, ubc_zerorange_process, NULL);
			}

			off += bytelen;
			todo -= bytelen;
			endoff = off & (PAGE_SIZE - 1);

			/*
			 * zero out the remaining portion of the final page
			 * (if any).
			 */
			if (todo == 0 && endoff != 0) {
				vsize_t zlen = PAGE_SIZE - endoff;
				(void) uvm_direct_process(pgs + npages - 1, 1,
				    off, zlen, ubc_zerorange_process, NULL);
			}
		} else {
			off += bytelen;
			todo -= bytelen;
		}

		ubc_direct_release(uobj, flags, pgs, npages);

		if (error != 0 && ISSET(flags, UBC_PARTIALOK)) {
			break;
		}
	}

	return error;
}

static void __noinline
ubc_zerorange_direct(struct uvm_object *uobj, off_t off, size_t todo, int flags)
{
	int error, npages;
	struct vm_page *pgs[howmany(ubc_winsize, MIN_PAGE_SIZE)];

	flags |= UBC_WRITE;

	error = 0;
	while (todo > 0) {
		vsize_t bytelen = todo;

		error = ubc_alloc_direct(uobj, off, &bytelen, UVM_ADV_NORMAL,
		    flags, pgs, &npages);
		if (error != 0) {
			/* can't do anything, failed to get the pages */
			break;
		}

		error = uvm_direct_process(pgs, npages, off, bytelen,
		    ubc_zerorange_process, NULL);

		ubc_direct_release(uobj, flags, pgs, npages);

		off += bytelen;
		todo -= bytelen;
	}
}

#endif /* UBC_USE_PMAP_DIRECT */

/*
 * ubc_purge: disassociate ubc_map structures from an empty uvm_object.
 */
void
ubc_purge(struct uvm_object *uobj)
{
	struct ubc_map *umap, *next;
	struct mm_page *pg;
	vaddr_t va;

	KASSERT(uobj->uo_npages == 0);

	// Safe to check without lock held, as ubc_alloc() removes
	// the mapping and list entry in the correct order.
	if (__predict_true(uobj->uo_ubc.lh_first == NULL)) {
		return;
	}
	rw_enter(ubc_object.uobj.vmobjlock, RW_WRITER);
	
	for (umap = uobj->uo_ubc.lh_first; umap != NULL; umap = next) {
		KASSERT(umap->refcount == 0);
#if 0
		for (int i = 0; i < UBC_NWINS; i++) {
			pg = umap->pages[i];
		}
#endif	
		umap->flags &= ~UMAP_MAPPING_CACHED;
		umap->uobj = NULL;
		next = umap->next;
		umap->prev = NULL;
		umap->next = NULL;
	}
	
	uobj->uo_ubc.lh_first = NULL;

	rw_exit(ubc_object.uobj.vmobjlock);
}

#ifndef UBC_NO_HASH
static int
ubchash_stats(struct hashstat_sysctl *hs, bool fill)
{
	struct ubc_map *umap;
	uint64_t chain;

	strlcpy(hs->hash_name, "ubchash", sizeof(hs->hash_name));
	strlcpy(hs->hash_desc, "ubc object hash", sizeof(hs->hash_desc));
	if (!fill)
		return 0;

	hs->hash_size = ubc_object.hashmask + 1;

	for (size_t i = 0; i < hs->hash_size; i++) {
		chain = 0;
		rw_enter(ubc_object.uobj.vmobjlock, RW_READER);
		LIST_FOREACH(umap, &ubc_object.hash[i], hash) {
			chain++;
		}
		rw_exit(ubc_object.uobj.vmobjlock);
		if (chain > 0) {
			hs->hash_used++;
			hs->hash_items += chain;
			if (chain > hs->hash_maxchain)
				hs->hash_maxchain = chain;
		}
		preempt_point();
	}

	return 0;
}
#endif
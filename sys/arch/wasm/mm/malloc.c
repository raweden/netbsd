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

#include <stdint.h>
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


#include "arch/wasm/include/vmparam.h"
#include "arch/wasm/include/wasm_inst.h"
#include "libkern/libkern.h"
#include "mm.h"
#include "param.h"
#include "systm.h"

#ifdef __WASM
#define __WASM_BUILTIN(symbol) __attribute__((import_module("__builtin"), import_name(#symbol)))
int wasm_random_source(void *, int, int *) __attribute__((import_module("kern"), import_name("random_source")));
int wasm_kern_ioctl(int cmd, void *argp) __attribute__((import_module("kern"), import_name("exec_ioctl")));
#else
__WASM_BUILTIN(x)
#endif

#ifndef KM_MALLOC_USE_SMALLPOOLS
#define KM_MALLOC_USE_SMALLPOOLS 0
#endif



/**
 * translates to `memory.fill` instruction in post-edit (or link-time with ylinker)
 *
 * @param dst The destination address.
 * @param val The value to use as fill
 * @param len The number of bytes to fill.
 */
void wasm_memory_fill(void * dst, int32_t val, uint32_t len) __WASM_BUILTIN(memory_fill);

#if KM_MALLOC_USE_SMALLPOOLS

#define KMEM_POOLCNT 8
#define KMEM_POOLMIN 4
#define KMEM_POOLMAX 128

static struct pool kmem_pools[KMEM_POOLCNT];
u_int16_t kmem_poolsizes[KMEM_POOLCNT] = {4, 8, 16, 32, 48, 64, 96, 128};
// 4, 8, 16, 32, 48, 64, 96, 128

#endif

// dlmalloc

#ifndef MALLOC_FAILURE_ACTION
#define MALLOC_FAILURE_ACTION
#endif /* MALLOC_FAILURE_ACTION */

#define DEFAULT_GRANULARITY (0)

#ifndef NO_SEGMENT_TRAVERSAL
#define NO_SEGMENT_TRAVERSAL 0
#endif /* NO_SEGMENT_TRAVERSAL */

#define MAX_SIZE_T           (~(size_t)0)

#define MALLOC_ALIGNMENT ((size_t)8U)

#define SIZE_T_SIZE         (sizeof(size_t))
#define SIZE_T_BITSIZE      (sizeof(size_t) << 3)

#define SIZE_T_ZERO         ((size_t)0)
#define SIZE_T_ONE          ((size_t)1)
#define SIZE_T_TWO          ((size_t)2)
#define SIZE_T_FOUR         ((size_t)4)
#define TWO_SIZE_T_SIZES    (SIZE_T_SIZE<<1)
#define FOUR_SIZE_T_SIZES   (SIZE_T_SIZE<<2)
#define SIX_SIZE_T_SIZES    (FOUR_SIZE_T_SIZES+TWO_SIZE_T_SIZES)
#define HALF_MAX_SIZE_T     (MAX_SIZE_T / 2U)

#define CHUNK_ALIGN_MASK    (MALLOC_ALIGNMENT - SIZE_T_ONE)
#define CHUNK_OVERHEAD      (SIZE_T_SIZE)

/* MMapped chunks need a second word of overhead ... */
#define MMAP_CHUNK_OVERHEAD (TWO_SIZE_T_SIZES)
/* ... and additional padding for fake next-chunk at foot */
#define MMAP_FOOT_PAD       (FOUR_SIZE_T_SIZES)

#define NSMALLBINS        (32U)
#define NTREEBINS         (32U)
#define SMALLBIN_SHIFT    (3U)
#define SMALLBIN_WIDTH    (SIZE_T_ONE << SMALLBIN_SHIFT)
#define TREEBIN_SHIFT     (8U)
#define MIN_LARGE_SIZE    (SIZE_T_ONE << TREEBIN_SHIFT)
#define MAX_SMALL_SIZE    (MIN_LARGE_SIZE - SIZE_T_ONE)
#define MAX_SMALL_REQUEST (MAX_SMALL_SIZE - CHUNK_ALIGN_MASK - CHUNK_OVERHEAD)

/* The smallest size we can malloc is an aligned minimal chunk */
#define MIN_CHUNK_SIZE\
  ((MCHUNK_SIZE + CHUNK_ALIGN_MASK) & ~CHUNK_ALIGN_MASK)

/* The bit mask value corresponding to MALLOC_ALIGNMENT */
#define CHUNK_ALIGN_MASK    (MALLOC_ALIGNMENT - SIZE_T_ONE)

/* True if address a has acceptable alignment */
#define is_aligned(A)       (((size_t)((A)) & (CHUNK_ALIGN_MASK)) == 0)

/* the number of bytes to offset an address to align it */
#define align_offset(A)\
 ((((size_t)(A) & CHUNK_ALIGN_MASK) == 0)? 0 :\
  ((MALLOC_ALIGNMENT - ((size_t)(A) & CHUNK_ALIGN_MASK)) & CHUNK_ALIGN_MASK))

#define MFAIL                   ((void*)(MAX_SIZE_T))
#define MMAP(s)                 MFAIL
#define MUNMAP(a, s)            (-1)
#define DIRECT_MMAP(s)          MFAIL
#define CALL_DIRECT_MMAP(s)     DIRECT_MMAP(s)
#define CALL_MMAP(s)            MMAP(s)
#define CALL_MUNMAP(a, s)       MUNMAP((a), (s))

/* conversion from malloc headers to user pointers, and back */
#define chunk2mem(p)        ((void*)((char*)(p)       + TWO_SIZE_T_SIZES))
#define mem2chunk(mem)      ((struct malloc_chunk*)((char*)(mem) - TWO_SIZE_T_SIZES))
/* chunk associated with aligned address A */
#define align_as_chunk(A)   (struct malloc_chunk*)((A) + align_offset(chunk2mem(A)))

/* Bounds on request (not chunk) sizes. */
#define MAX_REQUEST         ((-MIN_CHUNK_SIZE) << 2)
#define MIN_REQUEST         (MIN_CHUNK_SIZE - CHUNK_OVERHEAD - SIZE_T_ONE)

/* pad request bytes into a usable size */
#define pad_request(req) \
   (((req) + CHUNK_OVERHEAD + CHUNK_ALIGN_MASK) & ~CHUNK_ALIGN_MASK)

/* pad request, checking for minimum (but not maximum) */
#define request2size(req) \
  (((req) < MIN_REQUEST)? MIN_CHUNK_SIZE : pad_request(req))

#define MAX_RELEASE_CHECK_RATE MAX_SIZE_T

// usage definable flags
#define MORECORE_CONTIGUOUS     (0)
#define DEFAULT_MMAP_THRESHOLD  MAX_SIZE_T
#define DEFAULT_TRIM_THRESHOLD  MAX_SIZE_T
#define USE_LOCK_BIT            (2U)
#define USE_MMAP_BIT            (SIZE_T_ZERO)

#define USE_NONCONTIGUOUS_BIT   (4U)

#define EXTERN_BIT              (8U)

#define PINUSE_BIT          (SIZE_T_ONE)
#define CINUSE_BIT          (SIZE_T_TWO)
#define FLAG4_BIT           (SIZE_T_FOUR)
#define INUSE_BITS          (PINUSE_BIT|CINUSE_BIT)
#define FLAG_BITS           (PINUSE_BIT|CINUSE_BIT|FLAG4_BIT)

/* Head value for fenceposts */
#define FENCEPOST_HEAD      (INUSE_BITS|SIZE_T_SIZE)

/* extraction of fields from head words */
#define cinuse(p)           ((p)->head & CINUSE_BIT)
#define pinuse(p)           ((p)->head & PINUSE_BIT)
#define flag4inuse(p)       ((p)->head & FLAG4_BIT)
#define is_inuse(p)         (((p)->head & INUSE_BITS) != PINUSE_BIT)
#define is_mmapped(p)       (((p)->head & INUSE_BITS) == 0)

#define chunksize(p)        ((p)->head & ~(FLAG_BITS))

#define clear_pinuse(p)     ((p)->head &= ~PINUSE_BIT)
#define set_flag4(p)        ((p)->head |= FLAG4_BIT)
#define clear_flag4(p)      ((p)->head &= ~FLAG4_BIT)

/* Treat space at ptr +/- offset as a chunk */
#define chunk_plus_offset(p, s)  ((struct malloc_chunk*)(((char*)(p)) + (s)))
#define chunk_minus_offset(p, s) ((struct malloc_chunk*)(((char*)(p)) - (s)))

/* Ptr to next or previous physical malloc_chunk. */
#define next_chunk(p) ((struct malloc_chunk*)( ((char*)(p)) + ((p)->head & ~FLAG_BITS)))
#define prev_chunk(p) ((struct malloc_chunk*)( ((char*)(p)) - ((p)->prev_foot) ))

/* extract next chunk's pinuse bit */
#define next_pinuse(p)  ((next_chunk(p)->head) & PINUSE_BIT)

/* Get/set size at footer */
#define get_foot(p, s)  (((struct malloc_chunk*)((char*)(p) + (s)))->prev_foot)
#define set_foot(p, s)  (((struct malloc_chunk*)((char*)(p) + (s)))->prev_foot = (s))

/* Set size, pinuse bit, and foot */
#define set_size_and_pinuse_of_free_chunk(p, s)\
  ((p)->head = (s|PINUSE_BIT), set_foot(p, s))

/* Set size, pinuse bit, foot, and clear next pinuse */
#define set_free_with_pinuse(p, s, n)\
  (clear_pinuse(n), set_size_and_pinuse_of_free_chunk(p, s))

typedef unsigned int bindex_t;
typedef unsigned int binmap_t;
typedef unsigned int flag_t; 

#if 0
struct malloc_chunk {
  size_t               prev_foot;  /* Size of previous chunk (if free).  */
  size_t               head;       /* Size and inuse bits. */
  struct malloc_chunk* fd;         /* double links -- used only if free. */
  struct malloc_chunk* bk;
};
#endif

#define MCHUNK_SIZE (sizeof(struct malloc_chunk))

#if 0
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
#endif

#define leftmost_child(t) ((t)->child[0] != 0? (t)->child[0] : (t)->child[1])

#if 0
struct malloc_segment {
  char*        base;             /* base address */
  size_t       size;             /* allocated size */
  struct malloc_segment* next;   /* ptr to next segment */
  flag_t       sflags;           /* mmap and extern flag */
};
#endif

#define is_mmapped_segment(S)  ((S)->sflags & USE_MMAP_BIT)
#define is_extern_segment(S)   ((S)->sflags & EXTERN_BIT)

#if 0
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
#endif

typedef struct malloc_state*    mstate;

#ifndef VMEM_NAME_MAX
#define VMEM_NAME_MAX 16
#endif

#if 0
struct mm_arena {
    struct malloc_state mstate;
    kmutex_t malloc_lock;
    struct mm_page *first_page;
    struct mm_page *last_page;
    char mm_name[VMEM_NAME_MAX + 1];
};
#endif

typedef struct mm_arena mm_arena_t;

struct malloc_params {
    size_t magic;
    size_t page_size;
    size_t granularity;
    size_t mmap_threshold;
    size_t trim_threshold;
    flag_t default_mflags;
};

static struct mm_arena __global_arena;
static struct malloc_params __mparams;

// 

#ifndef CORRUPTION_ERROR_ACTION
#define CORRUPTION_ERROR_ACTION(m) panic("CORRUPTION_ERROR_ACTION")
#endif /* CORRUPTION_ERROR_ACTION */

#ifndef USAGE_ERROR_ACTION
#define USAGE_ERROR_ACTION(m,p) panic("USAGE_ERROR_ACTION")
#endif /* USAGE_ERROR_ACTION */

#define is_small(s)         (((s) >> SMALLBIN_SHIFT) < NSMALLBINS)
#define small_index(s)      (bindex_t)((s)  >> SMALLBIN_SHIFT)
#define small_index2size(i) ((i)  << SMALLBIN_SHIFT)
#define MIN_SMALL_INDEX     (small_index(MIN_CHUNK_SIZE))

/* addressing by index. See above about smallbin repositioning */
#define smallbin_at(M, i)   ((struct malloc_chunk*)((char*)&((M)->smallbins[(i)<<1])))
#define treebin_at(M,i)     (&((M)->treebins[i]))

#define compute_tree_index(S, I)\
{\
  size_t X = S >> TREEBIN_SHIFT;\
  if (X == 0)\
    I = 0;\
  else if (X > 0xFFFF)\
    I = NTREEBINS-1;\
  else {\
    unsigned int Y = (unsigned int)X;\
    unsigned int N = ((Y - 0x100) >> 16) & 8;\
    unsigned int K = (((Y <<= N) - 0x1000) >> 16) & 4;\
    N += K;\
    N += K = (((Y <<= K) - 0x4000) >> 16) & 2;\
    K = 14 - N + ((Y <<= K) >> 15);\
    I = (K << 1) + ((S >> (K + (TREEBIN_SHIFT-1)) & 1));\
  }\
}

/* Bit representing maximum resolved size in a treebin at i */
#define bit_for_tree_index(i) \
   (i == NTREEBINS-1)? (SIZE_T_BITSIZE-1) : (((i) >> 1) + TREEBIN_SHIFT - 2)

/* Shift placing maximum resolved bit in a treebin at i as sign bit */
#define leftshift_for_tree_index(i) \
   ((i == NTREEBINS-1)? 0 : \
    ((SIZE_T_BITSIZE-SIZE_T_ONE) - (((i) >> 1) + TREEBIN_SHIFT - 2)))

/* The size of the smallest chunk held in bin with index i */
#define minsize_for_tree_index(i) \
   ((SIZE_T_ONE << (((i) >> 1) + TREEBIN_SHIFT)) |  \
   (((size_t)((i) & SIZE_T_ONE)) << (((i) >> 1) + TREEBIN_SHIFT - 1)))

/* bit corresponding to given index */
#define idx2bit(i)              ((binmap_t)(1) << (i))

/* Mark/Clear bits with given index */
#define mark_smallmap(M,i)      ((M)->smallmap |=  idx2bit(i))
#define clear_smallmap(M,i)     ((M)->smallmap &= ~idx2bit(i))
#define smallmap_is_marked(M,i) ((M)->smallmap &   idx2bit(i))

#define mark_treemap(M,i)       ((M)->treemap  |=  idx2bit(i))
#define clear_treemap(M,i)      ((M)->treemap  &= ~idx2bit(i))
#define treemap_is_marked(M,i)  ((M)->treemap  &   idx2bit(i))

/* isolate the least set bit of a bitmap */
#define least_bit(x)         ((x) & -(x))

/* mask with all bits to left of least bit of x on */
#define left_bits(x)         ((x<<1) | -(x<<1))

/* mask with all bits to left of or equal to least bit of x on */
#define same_or_left_bits(x) ((x) | -(x))


#define ok_address(M, a) (1)
#define ok_next(b, n)    (1)
#define ok_inuse(p)      (1)
#define ok_pinuse(p)     (1)

#if (FOOTERS && !INSECURE)
/* Check if (alleged) mstate m has expected magic field */
#define ok_magic(M)      ((M)->magic == mparams.magic)
#else  /* (FOOTERS && !INSECURE) */
#define ok_magic(M)      (1)
#endif /* (FOOTERS && !INSECURE) */

/* In gcc, use __builtin_expect to minimize impact of checks */
#if !INSECURE
#if defined(__GNUC__) && __GNUC__ >= 3
#define RTCHECK(e)  __builtin_expect(e, 1)
#else /* GNUC */
#define RTCHECK(e)  (e)
#endif /* GNUC */
#else /* !INSECURE */
#define RTCHECK(e)  (1)
#endif /* !INSECURE */

/* Set cinuse bit and pinuse bit of next chunk */
#define set_inuse(M,p,s)\
  ((p)->head = (((p)->head & PINUSE_BIT)|s|CINUSE_BIT),\
  ((struct malloc_chunk*)(((char*)(p)) + (s)))->head |= PINUSE_BIT)

/* Set cinuse and pinuse of this chunk and pinuse of next chunk */
#define set_inuse_and_pinuse(M,p,s)\
  ((p)->head = (s|PINUSE_BIT|CINUSE_BIT),\
  ((struct malloc_chunk*)(((char*)(p)) + (s)))->head |= PINUSE_BIT)

/* Set size, cinuse and pinuse bit of this chunk */
#define set_size_and_pinuse_of_inuse_chunk(M, p, s)\
  ((p)->head = (s|PINUSE_BIT|CINUSE_BIT))

/* ----------------------- Operations on smallbins ----------------------- */

/*
  Various forms of linking and unlinking are defined as macros.  Even
  the ones for trees, which are very long but have very short typical
  paths.  This is ugly but reduces reliance on inlining support of
  compilers.
*/

/* Link a free chunk into a smallbin  */
#define insert_small_chunk(M, P, S) {\
  bindex_t I  = small_index(S);\
  struct malloc_chunk *B = smallbin_at(M, I);\
  struct malloc_chunk *F = B;\
  assert(S >= MIN_CHUNK_SIZE);\
  if (!smallmap_is_marked(M, I))\
    mark_smallmap(M, I);\
  else if (RTCHECK(ok_address(M, B->fd)))\
    F = B->fd;\
  else {\
    CORRUPTION_ERROR_ACTION(M);\
  }\
  B->fd = P;\
  F->bk = P;\
  P->fd = F;\
  P->bk = B;\
}

/* Unlink a chunk from a smallbin  */
#define unlink_small_chunk(M, P, S) {\
  struct malloc_chunk *F = P->fd;\
  struct malloc_chunk *B = P->bk;\
  bindex_t I = small_index(S);\
  assert(P != B);\
  assert(P != F);\
  assert(chunksize(P) == small_index2size(I));\
  if (RTCHECK(F == smallbin_at(M,I) || (ok_address(M, F) && F->bk == P))) { \
    if (B == F) {\
      clear_smallmap(M, I);\
    }\
    else if (RTCHECK(B == smallbin_at(M,I) ||\
                     (ok_address(M, B) && B->fd == P))) {\
      F->bk = B;\
      B->fd = F;\
    }\
    else {\
      CORRUPTION_ERROR_ACTION(M);\
    }\
  }\
  else {\
    CORRUPTION_ERROR_ACTION(M);\
  }\
}

/* Unlink the first chunk from a smallbin */
#define unlink_first_small_chunk(M, B, P, I) {\
  struct malloc_chunk *F = P->fd;\
  assert(P != B);\
  assert(P != F);\
  assert(chunksize(P) == small_index2size(I));\
  if (B == F) {\
    clear_smallmap(M, I);\
  }\
  else if (RTCHECK(ok_address(M, F) && F->bk == P)) {\
    F->bk = B;\
    B->fd = F;\
  }\
  else {\
    CORRUPTION_ERROR_ACTION(M);\
  }\
}

/* Replace dv node, binning the old one */
/* Used only when dvsize known to be small */
#define replace_dv(M, P, S) {           \
  size_t DVS = M->dvsize;               \
  assert(is_small(DVS));                \
  if (DVS != 0) {                       \
    struct malloc_chunk *DV = M->dv;    \
    insert_small_chunk(M, DV, DVS);     \
  }                                     \
  M->dvsize = S;                        \
  M->dv = P;                            \
}

/* ------------------------- Operations on trees ------------------------- */

/* Insert chunk into tree */
#define insert_large_chunk(M, X, S) {\
  struct malloc_tree_chunk** H;\
  bindex_t I;\
  compute_tree_index(S, I);\
  H = treebin_at(M, I);\
  X->index = I;\
  X->child[0] = X->child[1] = 0;\
  if (!treemap_is_marked(M, I)) {\
    mark_treemap(M, I);\
    *H = X;\
    X->parent = (struct malloc_tree_chunk*)H;\
    X->fd = X->bk = X;\
  }\
  else {\
    struct malloc_tree_chunk *T = *H;\
    size_t K = S << leftshift_for_tree_index(I);\
    for (;;) {\
      if (chunksize(T) != S) {\
        struct malloc_tree_chunk **C = &(T->child[(K >> (SIZE_T_BITSIZE-SIZE_T_ONE)) & 1]);\
        K <<= 1;\
        if (*C != 0)\
          T = *C;\
        else if (RTCHECK(ok_address(M, C))) {\
          *C = X;\
          X->parent = T;\
          X->fd = X->bk = X;\
          break;\
        }\
        else {\
          CORRUPTION_ERROR_ACTION(M);\
          break;\
        }\
      }\
      else {\
        struct malloc_tree_chunk *F = T->fd;\
        if (RTCHECK(ok_address(M, T) && ok_address(M, F))) {\
          T->fd = F->bk = X;\
          X->fd = F;\
          X->bk = T;\
          X->parent = 0;\
          break;\
        }\
        else {\
          CORRUPTION_ERROR_ACTION(M);\
          break;\
        }\
      }\
    }\
  }\
}

/*
  Unlink steps:

  1. If x is a chained node, unlink it from its same-sized fd/bk links
     and choose its bk node as its replacement.
  2. If x was the last node of its size, but not a leaf node, it must
     be replaced with a leaf node (not merely one with an open left or
     right), to make sure that lefts and rights of descendents
     correspond properly to bit masks.  We use the rightmost descendent
     of x.  We could use any other leaf, but this is easy to locate and
     tends to counteract removal of leftmosts elsewhere, and so keeps
     paths shorter than minimally guaranteed.  This doesn't loop much
     because on average a node in a tree is near the bottom.
  3. If x is the base of a chain (i.e., has parent links) relink
     x's parent and children to x's replacement (or null if none).
*/

#define unlink_large_chunk(M, X) {\
	struct malloc_tree_chunk* XP = X->parent;\
	struct malloc_tree_chunk* R;\
	if (X->bk != X) {\
		struct malloc_tree_chunk* F = X->fd;\
		R = X->bk;\
		if (RTCHECK(ok_address(M, F) && F->bk == X && R->fd == X)) {\
			F->bk = R;\
			R->fd = F;\
		}\
		else {\
			CORRUPTION_ERROR_ACTION(M);\
		}\
	}\
	else {\
		struct malloc_tree_chunk** RP;\
		if (((R = *(RP = &(X->child[1]))) != 0) ||\
				((R = *(RP = &(X->child[0]))) != 0)) {\
			struct malloc_tree_chunk** CP;\
			while ((*(CP = &(R->child[1])) != 0) ||\
						 (*(CP = &(R->child[0])) != 0)) {\
				R = *(RP = CP);\
			}\
			if (RTCHECK(ok_address(M, RP)))\
				*RP = 0;\
			else {\
				CORRUPTION_ERROR_ACTION(M);\
			}\
		}\
	}\
	if (XP != 0) {\
		struct malloc_tree_chunk** H = treebin_at(M, X->index);\
		if (X == *H) {\
			if ((*H = R) == 0) \
				clear_treemap(M, X->index);\
		}\
		else if (RTCHECK(ok_address(M, XP))) {\
			if (XP->child[0] == X) \
				XP->child[0] = R;\
			else \
				XP->child[1] = R;\
		}\
		else\
			CORRUPTION_ERROR_ACTION(M);\
		if (R != 0) {\
			if (RTCHECK(ok_address(M, R))) {\
				struct malloc_tree_chunk* C0, *C1;\
				R->parent = XP;\
				if ((C0 = X->child[0]) != 0) {\
					if (RTCHECK(ok_address(M, C0))) {\
						R->child[0] = C0;\
						C0->parent = R;\
					}\
					else\
						CORRUPTION_ERROR_ACTION(M);\
				}\
				if ((C1 = X->child[1]) != 0) {\
					if (RTCHECK(ok_address(M, C1))) {\
						R->child[1] = C1;\
						C1->parent = R;\
					}\
					else\
						CORRUPTION_ERROR_ACTION(M);\
				}\
			}\
			else\
				CORRUPTION_ERROR_ACTION(M);\
		}\
	}\
}
#if 0
static inline void
unlink_large_chunk(struct malloc_state *M, struct malloc_tree_chunk *X)
{
	struct malloc_tree_chunk* XP = X->parent;
	struct malloc_tree_chunk* R;
	if (X->bk != X) {
		struct malloc_tree_chunk* F = X->fd;
		R = X->bk;
		if (RTCHECK(ok_address(M, F) && F->bk == X && R->fd == X)) {
			F->bk = R;
			R->fd = F;
		}
		else {
			CORRUPTION_ERROR_ACTION(M);
		}
	}
	else {
		struct malloc_tree_chunk** RP;
		if (((R = *(RP = &(X->child[1]))) != 0) ||
				((R = *(RP = &(X->child[0]))) != 0)) {
			struct malloc_tree_chunk** CP;
			while ((*(CP = &(R->child[1])) != 0) ||
						 (*(CP = &(R->child[0])) != 0)) {
				R = *(RP = CP);
			}
			if (RTCHECK(ok_address(M, RP)))
				*RP = 0;
			else {
				CORRUPTION_ERROR_ACTION(M);
			}
		}
	}
	if (XP != 0) {
		struct malloc_tree_chunk** H = treebin_at(M, X->index);
		if (X == *H) {
			if ((*H = R) == 0) 
				clear_treemap(M, X->index);
		}
		else if (RTCHECK(ok_address(M, XP))) {
			if (XP->child[0] == X) 
				XP->child[0] = R;
			else 
				XP->child[1] = R;
		}
		else
			CORRUPTION_ERROR_ACTION(M);
		if (R != 0) {
			if (RTCHECK(ok_address(M, R))) {
				struct malloc_tree_chunk* C0, *C1;
				R->parent = XP;
				if ((C0 = X->child[0]) != 0) {
					if (RTCHECK(ok_address(M, C0))) {
						R->child[0] = C0;
						C0->parent = R;
					}
					else
						CORRUPTION_ERROR_ACTION(M);
				}
				if ((C1 = X->child[1]) != 0) {
					if (RTCHECK(ok_address(M, C1))) {
						R->child[1] = C1;
						C1->parent = R;
					}
					else
						CORRUPTION_ERROR_ACTION(M);
				}
			}
			else
				CORRUPTION_ERROR_ACTION(M);
		}
	}
}
#endif

/*  True if segment S holds address A */
#define segment_holds(S, A)\
	((char*)(A) >= S->base && (char*)(A) < S->base + S->size)

/* Return segment holding given address */
static struct malloc_segment *
segment_holding(mstate m, char* addr)
{
	struct malloc_segment *sp = &m->seg;
	for (;;) {
		if (addr >= sp->base && addr < sp->base + sp->size)
			return sp;
		if ((sp = sp->next) == 0)
			return 0;
	}
}

/* Return true if segment contains a segment link */
static int
has_segment_link(mstate m, struct malloc_segment *ss)
{
	struct malloc_segment * sp = &m->seg;
	for (;;) {
		if ((char*)sp >= ss->base && (char*)sp < ss->base + ss->size)
			return 1;
		if ((sp = sp->next) == 0)
			return 0;
	}
}

/*
  TOP_FOOT_SIZE is padding at the end of a segment, including space
  that may be needed to place segment records and fenceposts when new
  noncontiguous segments are added.
*/
#define TOP_FOOT_SIZE\
  (align_offset(chunk2mem(0)) + pad_request(sizeof(struct malloc_segment)) + MIN_CHUNK_SIZE)

#define should_trim(M,s)  (0)

/* Relays to large vs small bin operations */

#define insert_chunk(M, P, S)\
  if (is_small(S)) insert_small_chunk(M, P, S)\
  else { struct malloc_tree_chunk *TP = (struct malloc_tree_chunk*)(P); insert_large_chunk(M, TP, S); }

#define unlink_chunk(M, P, S)\
  if (is_small(S)) unlink_small_chunk(M, P, S)\
  else { struct malloc_tree_chunk *TP = (struct malloc_tree_chunk*)(P); unlink_large_chunk(M, TP); }

#define check_free_chunk(M,P)
#define check_inuse_chunk(M,P)
#define check_malloced_chunk(M,P,N)
#define check_mmapped_chunk(M,P)
#define check_malloc_state(M)
#define check_top_chunk(M,P)

#define compute_bit2idx(X, I)           \
{                                       \
  unsigned int Y = X - 1;               \
  unsigned int K = Y >> (16-4) & 16;    \
  unsigned int N = K;        Y >>= K;   \
  N += K = Y >> (8-3) &  8;  Y >>= K;   \
  N += K = Y >> (4-2) &  4;  Y >>= K;   \
  N += K = Y >> (2-1) &  2;  Y >>= K;   \
  N += K = Y >> (1-0) &  1;  Y >>= K;   \
  I = (bindex_t)(N + Y);                \
}

/**
 * Initialize mparams
 */
static int 
init_mparams(void)
{
    if (__mparams.magic == 0) {
        size_t magic;
        size_t psize;
        size_t gsize;

        psize = PAGE_SIZE;
        gsize = ((DEFAULT_GRANULARITY != 0) ? DEFAULT_GRANULARITY : psize);

    /* Sanity-check configuration:
       size_t must be unsigned and as wide as pointer type.
       ints must be at least 4 bytes.
       alignment must be at least 8.
       Alignment, min chunk size, and page size must all be powers of 2.
    */
        if ((sizeof(size_t) != sizeof(char*)) ||
            (MAX_SIZE_T < MIN_CHUNK_SIZE)  ||
            (sizeof(int) < 4)  ||
            (MALLOC_ALIGNMENT < (size_t)8U) ||
            ((MALLOC_ALIGNMENT & (MALLOC_ALIGNMENT-SIZE_T_ONE)) != 0) ||
            ((MCHUNK_SIZE      & (MCHUNK_SIZE-SIZE_T_ONE))      != 0) ||
            ((gsize            & (gsize-SIZE_T_ONE))            != 0) ||
            ((psize            & (psize-SIZE_T_ONE))            != 0))
                panic("malloc init failed");

        __mparams.granularity = gsize;
        __mparams.page_size = psize;
        __mparams.mmap_threshold = DEFAULT_MMAP_THRESHOLD;
        __mparams.trim_threshold = DEFAULT_TRIM_THRESHOLD;
#if MORECORE_CONTIGUOUS
        mparams.default_mflags = USE_LOCK_BIT|USE_MMAP_BIT;
#else  /* MORECORE_CONTIGUOUS */
        __mparams.default_mflags = USE_LOCK_BIT | USE_MMAP_BIT | USE_NONCONTIGUOUS_BIT;
#endif /* MORECORE_CONTIGUOUS */

        // Set up lock for main malloc area
        __global_arena.mstate.mflags = __mparams.default_mflags;
        mutex_init(&__global_arena.mstate.mutex, MUTEX_SPIN, 0);

        {
            char buf[sizeof(size_t)];
            wasm_random_source(&buf, sizeof(buf), NULL);
            magic = *((size_t *) buf);
            magic |= (size_t)8U;    /* ensure nonzero */
            magic &= ~(size_t)7U;   /* improve chances of fault for bad values */
            /* Until memory modes commonly available, use volatile-write */
            (*(volatile size_t *)(&(__mparams.magic))) = magic;
        }
    }
    
    return 1;
}

#define is_global(m) ((m) == (&__global_arena.mstate))

#define is_initialized(M)  ((M)->top != 0)

// dlmalloc end


//

/**
 * Initialize top chunk and its size 
 */
static void
init_top(struct malloc_state *m, struct malloc_chunk *p, size_t psize)
{
    // Ensure alignment
    size_t offset = align_offset(chunk2mem(p));
    p = (struct malloc_chunk*)((char*)p + offset);
    psize -= offset;

    m->top = p;
    m->topsize = psize;
    p->head = psize | PINUSE_BIT;
    /* set size of fake trailing chunk holding overhead space only once */
    chunk_plus_offset(p, psize)->head = TOP_FOOT_SIZE;
    m->trim_check = __mparams.trim_threshold; /* reset on each update */
}

/**
 * Initialize bins for a new mstate that is otherwise zeroed out 
 */
static void
init_bins(struct malloc_state *m)
{
    // Establish circular links for smallbins
    bindex_t i;
    for (i = 0; i < NSMALLBINS; ++i) {
        struct malloc_chunk *bin = smallbin_at(m, i);
        bin->fd = bin->bk = bin;
    }
}

void
mm_kmem_init(void)
{
#if 0
    if (kmem_poolsizes[0] != KMEM_POOLMIN || kmem_poolsizes[KMEM_POOLCNT - 1] != KMEM_POOLMAX) {
        panic("kmem_poolsizes out of sync");
    }

    for (int i = 0; i < KMEM_POOLCNT; i++) {
        u_int16_t size = kmem_poolsizes[i];
        pool_init(&kmem_pools[i], size, size, 0, 0, "$kmem_pool", &pool_allocator_kmem, IPL_NONE);
    }
#endif
    uint32_t memsz;
    void *mem;
    struct mm_page *pg;
    int idx;

    mutex_init(&__global_arena.mstate.mutex, MUTEX_SPIN, 0);

#if KM_MALLOC_USE_SMALLPOOLS
    pool_init(&kmem_pools[0], 4, 4, 0, 0, "$kmem_pool@4", &pool_allocator_kmem, IPL_NONE);
    pool_init(&kmem_pools[1], 8, 8, 0, 0, "$kmem_pool@8", &pool_allocator_kmem, IPL_NONE);
    pool_init(&kmem_pools[2], 16, 16, 0, 0, "$kmem_pool@16", &pool_allocator_kmem, IPL_NONE);
    pool_init(&kmem_pools[3], 32, 32, 0, 0, "$kmem_pool@32", &pool_allocator_kmem, IPL_NONE);
    pool_init(&kmem_pools[4], 48, 48, 0, 0, "$kmem_pool@48", &pool_allocator_kmem, IPL_NONE);
    pool_init(&kmem_pools[5], 64, 64, 0, 0, "$kmem_pool@64", &pool_allocator_kmem, IPL_NONE);
    pool_init(&kmem_pools[6], 96, 96, 0, 0, "$kmem_pool@96", &pool_allocator_kmem, IPL_NONE);
    pool_init(&kmem_pools[7], 128, 128, 0, 0, "$kmem_pool@128", &pool_allocator_kmem, IPL_NONE);
#endif

    mem = kmem_page_alloc(16, 0);
    if (mem == NULL)
        panic("failed to alloc pages for malloc");
    memsz = (16 * PAGE_SIZE);
    __global_arena.first_page = paddr_to_page(mem);
    __global_arena.last_page = paddr_to_page(mem + memsz - 1);

    printf("%s init-mem-start: %p init-mem-end: %p\n", __func__, mem, mem + memsz);

    init_mparams();

    struct malloc_state *m = &__global_arena.mstate;
    m->least_addr = mem;
    m->seg.base = mem;
    m->seg.size = memsz;
    m->seg.sflags = 0;
    m->magic = __mparams.magic;
    m->release_checks = 4095;
    init_bins(m);
    init_top(m, (struct malloc_chunk *)mem, memsz - TOP_FOOT_SIZE);
}

#if KM_MALLOC_USE_SMALLPOOLS
static inline void *
kmem_pool_alloc(size_t size)
{
    if (size <= 4) {
        return pool_get(&kmem_pools[0], 0);
    } else if (size <= 8) {
        return pool_get(&kmem_pools[1], 0);
    } else if (size <= 16) {
        return pool_get(&kmem_pools[2], 0);
    } else if (size <= 32) {
        return pool_get(&kmem_pools[3], 0);
    } else if (size <= 48) {
        return pool_get(&kmem_pools[4], 0);
    } else if (size <= 64) {
        return pool_get(&kmem_pools[5], 0);
    } else if (size <= 96) {
        return pool_get(&kmem_pools[6], 0);
    } else if (size <= 128) {
        return pool_get(&kmem_pools[7], 0);
    }

    return NULL;
} 

static inline int
kmem_pool_free(size_t size, void *ptr)
{
    int err = -1;

    if (size <= 4) {
        pool_put(&kmem_pools[0], ptr);
        err = 0;
    } else if (size <= 8) {
        pool_put(&kmem_pools[1], ptr);
        err = 0;
    } else if (size <= 16) {
        pool_put(&kmem_pools[2], ptr);
        err = 0;
    } else if (size <= 32) {
        pool_put(&kmem_pools[3], ptr);
        err = 0;
    } else if (size <= 48) {
        pool_put(&kmem_pools[4], ptr);
        err = 0;
    } else if (size <= 64) {
        pool_put(&kmem_pools[5], ptr);
        err = 0;
    } else if (size <= 96) {
        pool_put(&kmem_pools[6], ptr);
        err = 0;
    } else if (size <= 128) {
        pool_put(&kmem_pools[7], ptr);
        err = 0;
    }

    return err;
}
#endif

static size_t
release_unused_segments(mstate m)
{
    return 0;
}

static int
sys_trim(mstate m, size_t pad)
{
    return false;
}

/* Allocate chunk and prepend remainder with chunk in successor base. */
static void *
prepend_alloc(mstate m, char* newbase, char* oldbase, size_t nb)
{
	struct malloc_chunk *p = align_as_chunk(newbase);
	struct malloc_chunk *oldfirst = align_as_chunk(oldbase);
	size_t psize = (char*)oldfirst - (char*)p;
	struct malloc_chunk *q = chunk_plus_offset(p, nb);
	size_t qsize = psize - nb;
	set_size_and_pinuse_of_inuse_chunk(m, p, nb);

	assert((char*)oldfirst > (char*)q);
	assert(pinuse(oldfirst));
	assert(qsize >= MIN_CHUNK_SIZE);

	/* consolidate remainder with first chunk of old base */
	if (oldfirst == m->top) {
		size_t tsize = m->topsize += qsize;
		m->top = q;
		q->head = tsize | PINUSE_BIT;
		check_top_chunk(m, q);
	} else if (oldfirst == m->dv) {
		size_t dsize = m->dvsize += qsize;
		m->dv = q;
		set_size_and_pinuse_of_free_chunk(q, dsize);
	} else {

		if (!is_inuse(oldfirst)) {
			size_t nsize = chunksize(oldfirst);
			unlink_chunk(m, oldfirst, nsize);
			oldfirst = chunk_plus_offset(oldfirst, nsize);
			qsize += nsize;
		}

		set_free_with_pinuse(q, qsize, oldfirst);
		insert_chunk(m, q, qsize);
		check_free_chunk(m, q);
	}

	check_malloced_chunk(m, chunk2mem(p), nb);
	return chunk2mem(p);
}

/* Add a segment to hold a new noncontiguous region */
static void
add_segment(mstate m, char* tbase, size_t tsize, flag_t mmapped)
{
	/* Determine locations and sizes of segment, fenceposts, old top */
	char* old_top = (char*)m->top;
	struct malloc_segment *oldsp = segment_holding(m, old_top);
	char* old_end = oldsp->base + oldsp->size;
	size_t ssize = pad_request(sizeof(struct malloc_segment));
	char* rawsp = old_end - (ssize + FOUR_SIZE_T_SIZES + CHUNK_ALIGN_MASK);
	size_t offset = align_offset(chunk2mem(rawsp));
	char* asp = rawsp + offset;
	char* csp = (asp < (old_top + MIN_CHUNK_SIZE))? old_top : asp;
	struct malloc_chunk *sp = (struct malloc_chunk*)csp;
	struct malloc_segment *ss = (struct malloc_segment*)(chunk2mem(sp));
	struct malloc_chunk *tnext = chunk_plus_offset(sp, ssize);
	struct malloc_chunk *p = tnext;
	int nfences = 0;

	/* reset top to new space */
	init_top(m, (struct malloc_chunk*)tbase, tsize - TOP_FOOT_SIZE);

	/* Set up segment record */
	assert(is_aligned(ss));
	set_size_and_pinuse_of_inuse_chunk(m, sp, ssize);
	*ss = m->seg; /* Push current record */
	m->seg.base = tbase;
	m->seg.size = tsize;
	m->seg.sflags = mmapped;
	m->seg.next = ss;

	/* Insert trailing fenceposts */
	for (;;) {
		struct malloc_chunk *nextp = chunk_plus_offset(p, SIZE_T_SIZE);
		p->head = FENCEPOST_HEAD;
		++nfences;
		if ((char*)(&(nextp->head)) < old_end)
			p = nextp;
		else
			break;
	}
	assert(nfences >= 2);

	/* Insert the rest of old top into a bin as an ordinary free chunk */
	if (csp != old_top) {
		struct malloc_chunk *q = (struct malloc_chunk*)old_top;
		size_t psize = csp - old_top;
		struct malloc_chunk *tn = chunk_plus_offset(q, psize);
		set_free_with_pinuse(q, psize, tn);
		insert_chunk(m, q, psize);
	}

	check_top_chunk(m, m->top);
}

#if 0
static void *
malloc_grow(struct malloc_state *m, size_t nb)
{
    struct mm_page *pg;
    void *tbase;
    flag_t mmap_flag = 0;
    uint32_t tsize, pagecnt;

    printf("%s called size = %zu\n", __func__, nb);

    pagecnt = howmany(nb, PAGE_SIZE);
    tbase = kmem_page_alloc(pagecnt, 0);
    if (tbase != NULL) {
        tsize = (pagecnt * PAGE_SIZE);
        pg = paddr_to_page(tbase);
        last_page->pageq.list.li_next = pg;
        pg = paddr_to_page(tbase + tsize - 1);
        last_page = pg;
    }


	if (tbase != NULL) {

		if ((m->footprint += tsize) > m->max_footprint)
			m->max_footprint = m->footprint;

#if 0
		if (!is_initialized(m)) { /* first-time initialization */
			if (m->least_addr == 0 || tbase < m->least_addr)
				m->least_addr = tbase;
			m->seg.base = tbase;
			m->seg.size = tsize;
			m->seg.sflags = mmap_flag;
			m->magic = mparams.magic;
			m->release_checks = MAX_RELEASE_CHECK_RATE;
			init_bins(m);
#if !ONLY_MSPACES
			if (is_global(m))
				init_top(m, (mchunkptr)tbase, tsize - TOP_FOOT_SIZE);
			else
#endif
			{
				/* Offset top by embedded malloc_state */
				struct malloc_chunk *mn = next_chunk(mem2chunk(m));
				init_top(m, mn, (size_t)((tbase + tsize) - (char*)mn) -TOP_FOOT_SIZE);
			}
		} else {
#endif
        // Try to merge with an existing segment
        struct malloc_segment *sp = &m->seg;
        // Only consider most recent segment if traversal suppressed
        while (sp != 0 && tbase != sp->base + sp->size)
            sp = (NO_SEGMENT_TRAVERSAL) ? 0 : sp->next;

        if (sp != 0 && !is_extern_segment(sp) && (sp->sflags & USE_MMAP_BIT) == mmap_flag && segment_holds(sp, m->top)) { /* append */
            sp->size += tsize;
            init_top(m, m->top, m->topsize + tsize);
        } else {
            if (tbase < m->least_addr)
                m->least_addr = tbase;
            sp = &m->seg;

            while (sp != 0 && sp->base != tbase + tsize)
                sp = (NO_SEGMENT_TRAVERSAL) ? 0 : sp->next;
            
            if (sp != 0 &&
                    !is_extern_segment(sp) &&
                    (sp->sflags & USE_MMAP_BIT) == mmap_flag) {
                char* oldbase = sp->base;
                sp->base = tbase;
                sp->size += tsize;
                return prepend_alloc(m, tbase, oldbase, nb);
            } else {
                add_segment(m, tbase, tsize, mmap_flag);
            }
        }
#if 0
		}
#endif

		if (nb < m->topsize) { /* Allocate from new or extended top space */
			size_t rsize = m->topsize -= nb;
			struct malloc_chunk *p = m->top;
			struct malloc_chunk *r = m->top = chunk_plus_offset(p, nb);
			r->head = rsize | PINUSE_BIT;
			set_size_and_pinuse_of_inuse_chunk(m, p, nb);
			check_top_chunk(m, m->top);
			check_malloced_chunk(m, chunk2mem(p), nb);
			return chunk2mem(p);
		}
	}

	MALLOC_FAILURE_ACTION;
	return NULL;
}
#endif

static void *
malloc_arena_grow(struct mm_arena *arena, size_t nb)
{
    struct malloc_state *mstate = &arena->mstate;
    struct mm_page *pg;
    void *tbase;
    flag_t mmap_flag = 0;
    uint32_t tsize, pagecnt;

#if __WASM_DEBUG_KERN_MEM
    printf("%s called size = %zu\n", __func__, nb);
#endif

    pagecnt = howmany(nb, PAGE_SIZE);
    tbase = kmem_page_alloc(pagecnt, 0);
    if (tbase != NULL) {
        tsize = (pagecnt * PAGE_SIZE);
        pg = paddr_to_page(tbase);
        if (arena->first_page == NULL) {
            arena->first_page = pg;
        } else {
            arena->last_page->pageq.list.li_next = pg;
        }
        pg = paddr_to_page(tbase + tsize - 1);
        arena->last_page = pg;
        atomic_add32(&arena->page_count, pagecnt);
    }


    if (tbase != NULL) {

        if ((mstate->footprint += tsize) > mstate->max_footprint)
            mstate->max_footprint = mstate->footprint;

        if (!is_initialized(mstate)) {
            /* first-time initialization */
            if (mstate->least_addr == 0 || tbase < (void *)mstate->least_addr)
                mstate->least_addr = tbase;
            mstate->seg.base = tbase;
            mstate->seg.size = tsize;
            mstate->seg.sflags = mmap_flag;
            mstate->magic = __mparams.magic;
            mstate->release_checks = MAX_RELEASE_CHECK_RATE;
            init_bins(mstate);
#if !ONLY_MSPACES
            if (is_global(mstate)) {
                init_top(mstate, (struct malloc_chunk*)tbase, tsize - TOP_FOOT_SIZE);
            } else
#endif
            {
                /* Offset top by embedded malloc_state */
                struct malloc_chunk *mn = next_chunk(mem2chunk(mstate));
                init_top(mstate, mn, (size_t)((tbase + tsize) - (void *)mn) - TOP_FOOT_SIZE);
            }
        } else {
            // Try to merge with an existing segment
            struct malloc_segment *sp = &mstate->seg;
            // Only consider most recent segment if traversal suppressed
            while (sp != 0 && tbase != sp->base + sp->size)
                sp = (NO_SEGMENT_TRAVERSAL) ? 0 : sp->next;

            if (sp != 0 && !is_extern_segment(sp) && (sp->sflags & USE_MMAP_BIT) == mmap_flag && segment_holds(sp, mstate->top)) { /* append */
                sp->size += tsize;
                init_top(mstate, mstate->top, mstate->topsize + tsize);
            } else {
                if (tbase < (void *)mstate->least_addr)
                    mstate->least_addr = tbase;
                sp = &mstate->seg;

                while (sp != 0 && sp->base != tbase + tsize)
                    sp = (NO_SEGMENT_TRAVERSAL) ? 0 : sp->next;
                
                if (sp != 0 &&
                        !is_extern_segment(sp) &&
                        (sp->sflags & USE_MMAP_BIT) == mmap_flag) {
                    char* oldbase = sp->base;
                    sp->base = tbase;
                    sp->size += tsize;
                    return prepend_alloc(mstate, tbase, oldbase, nb);
                } else {
                    add_segment(mstate, tbase, tsize, mmap_flag);
                }
            }
        }

        if (nb < mstate->topsize) { /* Allocate from new or extended top space */
            size_t rsize = mstate->topsize -= nb;
            struct malloc_chunk *p = mstate->top;
            struct malloc_chunk *r = mstate->top = chunk_plus_offset(p, nb);
            r->head = rsize | PINUSE_BIT;
            set_size_and_pinuse_of_inuse_chunk(mstate, p, nb);
            check_top_chunk(mstate, mstate->top);
            check_malloced_chunk(mstate, chunk2mem(p), nb);
            return chunk2mem(p);
        }
    }

    MALLOC_FAILURE_ACTION;
    return NULL;
}

/**
 * allocate a large request from the best fitting chunk in a treebin
 */
static void *
tmalloc_large(mstate m, size_t nb)
{
	struct malloc_tree_chunk *v = 0;
	size_t rsize = -nb; /* Unsigned negation */
	struct malloc_tree_chunk *t;
	bindex_t idx;
	compute_tree_index(nb, idx);
	if ((t = *treebin_at(m, idx)) != 0) {
		/* Traverse tree for this bin looking for node with size == nb */
		size_t sizebits = nb << leftshift_for_tree_index(idx);
		struct malloc_tree_chunk *rst = 0;  /* The deepest untaken right subtree */
		for (;;) {
			struct malloc_tree_chunk *rt;
			size_t trem = chunksize(t) - nb;
			if (trem < rsize) {
				v = t;
				if ((rsize = trem) == 0)
					break;
			}
			rt = t->child[1];
			t = t->child[(sizebits >> (SIZE_T_BITSIZE - SIZE_T_ONE)) & 1];
			if (rt != 0 && rt != t)
				rst = rt;
			if (t == 0) {
				t = rst; /* set t to least subtree holding sizes > nb */
				break;
			}
			sizebits <<= 1;
		}
	}
	if (t == 0 && v == 0) { /* set t to root of next non-empty treebin */
		binmap_t leftbits = left_bits(idx2bit(idx)) & m->treemap;
		if (leftbits != 0) {
			bindex_t i;
			binmap_t leastbit = least_bit(leftbits);
			compute_bit2idx(leastbit, i);
			t = *treebin_at(m, i);
		}
	}

	while (t != 0) { /* find smallest of tree or subtree */
		size_t trem = chunksize(t) - nb;
		if (trem < rsize) {
			rsize = trem;
			v = t;
		}
		t = leftmost_child(t);
	}

	/*  If dv is a better fit, return 0 so malloc will use it */
	if (v != 0 && rsize < (size_t)(m->dvsize - nb)) {
		if (RTCHECK(ok_address(m, v))) { /* split */
			struct malloc_chunk *r = chunk_plus_offset(v, nb);
			assert(chunksize(v) == rsize + nb);
			if (RTCHECK(ok_next(v, r))) {
				unlink_large_chunk(m, v);
				if (rsize < MIN_CHUNK_SIZE)
					set_inuse_and_pinuse(m, v, (rsize + nb));
				else {
					set_size_and_pinuse_of_inuse_chunk(m, v, nb);
					set_size_and_pinuse_of_free_chunk(r, rsize);
					insert_chunk(m, r, rsize);
				}
				return chunk2mem(v);
			}
		}
		CORRUPTION_ERROR_ACTION(m);
	}
	return 0;
}

/** 
 * allocate a small request from the best fitting chunk in a treebin 
 */
static void* tmalloc_small(mstate m, size_t nb) {
	struct malloc_tree_chunk *t, *v;
	size_t rsize;
	bindex_t i;
	binmap_t leastbit = least_bit(m->treemap);
	compute_bit2idx(leastbit, i);
	v = t = *treebin_at(m, i);
	rsize = chunksize(t) - nb;

	while ((t = leftmost_child(t)) != 0) {
		size_t trem = chunksize(t) - nb;
		if (trem < rsize) {
			rsize = trem;
			v = t;
		}
	}

	if (RTCHECK(ok_address(m, v))) {
		struct malloc_chunk *r = chunk_plus_offset(v, nb);
		assert(chunksize(v) == rsize + nb);
		if (RTCHECK(ok_next(v, r))) {
			unlink_large_chunk(m, v);
			if (rsize < MIN_CHUNK_SIZE)
				set_inuse_and_pinuse(m, v, (rsize + nb));
			else {
				set_size_and_pinuse_of_inuse_chunk(m, v, nb);
				set_size_and_pinuse_of_free_chunk(r, rsize);
				replace_dv(m, r, rsize);
			}
			return chunk2mem(v);
		}
	}

	CORRUPTION_ERROR_ACTION(m);
	return 0;
}

// used by allocation in both global and sub arenas

// try to merge both versions of malloc/free into one inline version
static inline int
mm_malloc_internal(struct mm_arena *vm, size_t size, vm_flag_t flags, uintptr_t *addrp)
{
    void *ptr;
    struct malloc_state *mstate = &vm->mstate;

    mutex_enter(&mstate->mutex);

    size_t nb;
    if (size <= MAX_SMALL_REQUEST) {
        bindex_t idx;
        binmap_t smallbits;
        nb = (size < MIN_REQUEST) ? MIN_CHUNK_SIZE : pad_request(size);
        idx = small_index(nb);
        smallbits = mstate->smallmap >> idx;

        if ((smallbits & 0x3U) != 0) { /* Remainderless fit to a smallbin. */
            struct malloc_chunk* b, *p;
            idx += ~smallbits & 1;       /* Uses next bin if idx empty */
            b = smallbin_at(mstate, idx);
            p = b->fd;
            assert(chunksize(p) == small_index2size(idx));
            unlink_first_small_chunk(mstate, b, p, idx);
            set_inuse_and_pinuse(mstate, p, small_index2size(idx));
            ptr = chunk2mem(p);
            check_malloced_chunk(mstate, ptr, nb);
            goto postaction;
        } else if (nb > mstate->dvsize) {

            if (smallbits != 0) { /* Use chunk in next nonempty smallbin */
                struct malloc_chunk* b, *p, *r;
                size_t rsize;
                bindex_t i;
                binmap_t leftbits = (smallbits << idx) & left_bits(idx2bit(idx));
                binmap_t leastbit = least_bit(leftbits);
                compute_bit2idx(leastbit, i);
                b = smallbin_at(mstate, i);
                p = b->fd;
                assert(chunksize(p) == small_index2size(i));
                unlink_first_small_chunk(mstate, b, p, i);
                rsize = small_index2size(i) - nb;
                /* Fit here cannot be remainderless if 4byte sizes */
                if (SIZE_T_SIZE != 4 && rsize < MIN_CHUNK_SIZE) {
                    set_inuse_and_pinuse(mstate, p, small_index2size(i));
                }else {
                    set_size_and_pinuse_of_inuse_chunk(mstate, p, nb);
                    r = chunk_plus_offset(p, nb);
                    set_size_and_pinuse_of_free_chunk(r, rsize);
                    replace_dv(mstate, r, rsize);
                }
                ptr = chunk2mem(p);
                check_malloced_chunk(mstate, ptr, nb);
                goto postaction;
            } else if (mstate->treemap != 0 && (ptr = tmalloc_small(mstate, nb)) != 0) {
                check_malloced_chunk(mstate, ptr, nb);
                goto postaction;
            }
        }
    } else if (size >= MAX_REQUEST) {
        nb = MAX_SIZE_T; /* Too big to allocate. Force failure (in sys alloc) */
    } else {
        nb = pad_request(size);
        if (mstate->treemap != 0 && (ptr = tmalloc_large(mstate, nb)) != 0) {
            check_malloced_chunk(mstate, ptr, nb);
            goto postaction;
        }
    }

    if (nb <= mstate->dvsize) {
        size_t rsize = mstate->dvsize - nb;
        struct malloc_chunk* p = mstate->dv;
        if (rsize >= MIN_CHUNK_SIZE) { /* split dv */
            struct malloc_chunk* r = mstate->dv = chunk_plus_offset(p, nb);
            mstate->dvsize = rsize;
            set_size_and_pinuse_of_free_chunk(r, rsize);
            set_size_and_pinuse_of_inuse_chunk(mstate, p, nb);
        } else { 
            // exhaust dv
            size_t dvs = mstate->dvsize;
            mstate->dvsize = 0;
            mstate->dv = 0;
            set_inuse_and_pinuse(mstate, p, dvs);
        }
        ptr = chunk2mem(p);
        check_malloced_chunk(mstate, ptr, nb);
        goto postaction;
    } else if (nb < mstate->topsize) { /* Split top */
        size_t rsize = mstate->topsize -= nb;
        struct malloc_chunk* p = mstate->top;
        struct malloc_chunk* r = mstate->top = chunk_plus_offset(p, nb);
        r->head = rsize | PINUSE_BIT;
        set_size_and_pinuse_of_inuse_chunk(mstate, p, nb);
        ptr = chunk2mem(p);
        check_top_chunk(mstate, mstate->top);
        check_malloced_chunk(mstate, ptr, nb);
        goto postaction;
    }

    ptr = malloc_arena_grow(vm, nb);

postaction:
    mutex_exit(&mstate->mutex);

#if __WASM_DEBUG_KERN_MEM
    printf("%s ptr = %p size = %zu", __func__, ptr, size);
#endif

    if (addrp != NULL)
        *addrp = (vmem_addr_t)ptr;

    return 0;
}

static inline int
mm_free_internal(struct mm_arena *vm, void *ptr)
{
    int err;
    struct malloc_state *mstate = &vm->mstate;

	if (ptr != 0) {

        mutex_enter(&mstate->mutex);

		struct malloc_chunk *p  = mem2chunk(ptr);
        check_inuse_chunk(mstate, p);
        if (RTCHECK(ok_address(mstate, p) && ok_inuse(p))) {
            size_t psize = chunksize(p);
#if __WASM_DEBUG_KERN_MEM
            printf("%s ptr = %p size = %zu", __func__, ptr, psize);
#endif
            struct malloc_chunk *next = chunk_plus_offset(p, psize);
            if (!pinuse(p)) {
                size_t prevsize = p->prev_foot;
                if (is_mmapped(p)) {
                    psize += prevsize + MMAP_FOOT_PAD;
                    if (CALL_MUNMAP((char*)p - prevsize, psize) == 0)
                        mstate->footprint -= psize;
                    goto postaction;
                } else {
                    struct malloc_chunk *prev = chunk_minus_offset(p, prevsize);
                    psize += prevsize;
                    p = prev;
                    if (RTCHECK(ok_address(mstate, prev))) { /* consolidate backward */
                        if (p != mstate->dv) {
                            unlink_chunk(mstate, p, prevsize);
                        } else if ((next->head & INUSE_BITS) == INUSE_BITS) {
                            mstate->dvsize = psize;
                            set_free_with_pinuse(p, psize, next);
                            goto postaction;
                        }
                    } else {
                        goto erroraction;
                    }
                }
            }

            if (RTCHECK(ok_next(p, next) && ok_pinuse(next))) {
                if (!cinuse(next)) {  /* consolidate forward */
                    if (next == mstate->top) {
                        size_t tsize = mstate->topsize += psize;
                        mstate->top = p;
                        p->head = tsize | PINUSE_BIT;
                        if (p == mstate->dv) {
                            mstate->dv = 0;
                            mstate->dvsize = 0;
                        }
                        if (should_trim(mstate, tsize))
                            sys_trim(mstate, 0);
                        goto postaction;
                    } else if (next == mstate->dv) {
                        size_t dsize = mstate->dvsize += psize;
                        mstate->dv = p;
                        set_size_and_pinuse_of_free_chunk(p, dsize);
                        goto postaction;
                    } else {
                        size_t nsize = chunksize(next);
                        psize += nsize;
                        unlink_chunk(mstate, next, nsize);
                        set_size_and_pinuse_of_free_chunk(p, psize);
                        if (p == mstate->dv) {
                            mstate->dvsize = psize;
                            goto postaction;
                        }
                    }
                } else {
                    set_free_with_pinuse(p, psize, next);
                }

                if (is_small(psize)) {
                    insert_small_chunk(mstate, p, psize);
                    check_free_chunk(mstate, p);
                } else {
                    struct malloc_tree_chunk *tp = (struct malloc_tree_chunk *)p;
                    insert_large_chunk(mstate, tp, psize);
                    check_free_chunk(mstate, p);
                    if (--mstate->release_checks == 0)
                        release_unused_segments(mstate);
                }
                goto postaction;
            }
        }
    erroraction:
        USAGE_ERROR_ACTION(mstate, p);
    postaction:
        mutex_exit(&mstate->mutex);
        //POSTACTION(mstate);
    }

    return 0;
}

// kmem replacement

void *
kmem_alloc(size_t bytes, km_flag_t flags)
{
    if (bytes >= PAGE_SIZE) {
        wasm_kern_ioctl(537, (void *)bytes);
    }

    uintptr_t ptr;
    mm_malloc_internal(&__global_arena, bytes, flags, &ptr);

    return (void *)ptr;
}

void *
kmem_zalloc(size_t size, km_flag_t flags)
{
    uintptr_t addr;
    mm_malloc_internal(&__global_arena, size, flags, &addr);
    if (addr != 0) {
        wasm_memory_fill((void *)addr, 0, size);
    }

    return (void *)addr;
}

void kmem_free(void *ptr, size_t size)
{
    mm_free_internal(&__global_arena, ptr);
}

// memory arena (replacement for vmem_create/vmem_alloc/vmem_free)

struct mm_arena *
mm_arena_init(struct mm_arena *vm, const char *name,
    vmem_addr_t base, vmem_size_t size, vmem_size_t quantum,
    vmem_import_t *importfn, vmem_release_t *releasefn,
    vmem_t *arg, vmem_size_t qcache_max, vm_flag_t flags, int ipl)
{

    if (vm == NULL) {
        vm = kmem_zalloc(sizeof(struct mm_arena), 0);
    }
    if (vm == NULL) {
        return NULL;
    }

    mutex_init(&vm->malloc_lock, MUTEX_SPIN, 0);
    mutex_init(&vm->mstate.mutex, MUTEX_SPIN, 0);

    if (strlen(name) > 16) {
        // warn here
    }

    strlcpy(vm->mm_name, name, sizeof(vm->mm_name));

    vm->first_page = NULL;
    vm->last_page = NULL;


    return vm;
}

struct mm_arena *
mm_arena_create(const char *name, vmem_addr_t base, vmem_size_t size,
    vmem_size_t quantum, vmem_import_t *importfn, vmem_release_t *releasefn,
    vmem_t *source, vmem_size_t qcache_max, vm_flag_t flags, int ipl)
{

    return mm_arena_init(NULL, name, base, size, quantum, importfn, releasefn, source, qcache_max, flags, ipl);
}

struct mm_arena *
mm_arena_xcreate(const char *name, vmem_addr_t base, vmem_size_t size,
    vmem_size_t quantum, vmem_ximport_t *importfn, vmem_release_t *releasefn,
    vmem_t *source, vmem_size_t qcache_max, vm_flag_t flags, int ipl)
{

    return mm_arena_init(NULL, name, base, size, quantum, NULL, releasefn, source, qcache_max, flags, ipl);
}


// addrp returns ptr to allocated memory
int
mm_arena_alloc(struct mm_arena *vm, vmem_size_t size, vm_flag_t flags, vmem_addr_t *addrp)
{
    if (size >= PAGE_SIZE) {
        wasm_kern_ioctl(537, (void *)size);
    }

    return mm_malloc_internal(vm, size, flags, addrp);
}

// addrp returns ptr to allocated memory
int
mm_arena_xalloc(struct mm_arena *vm, const vmem_size_t size0, vmem_size_t align,
    const vmem_size_t phase, const vmem_size_t nocross,
    const vmem_addr_t minaddr, const vmem_addr_t maxaddr, const vm_flag_t flags,
    vmem_addr_t *addrp)
{
    if (size0 >= PAGE_SIZE) {
        wasm_kern_ioctl(537, (void *)size0);
    }

    return mm_malloc_internal(vm, size0, flags, addrp);
}

void
mm_arena_free(struct mm_arena *vm, vmem_addr_t addr, vmem_size_t size)
{
    mm_free_internal(vm, (void *)addr);
}

void mm_arena_destroy(struct mm_arena *vm)
{
    kmem_page_free(vm->first_page, vm->page_count);
    vm->first_page = NULL;
    vm->last_page = NULL;
    atomic_store32(&vm->page_count, 0);
}


struct mm_arena *
mm_arena_create_simple(const char *name, int *error)
{
    struct mm_arena *arena;
    uint32_t memsz;
    void *mem;
    struct mm_page *pg;
    int idx;

    mem = kmem_page_alloc(1, 0);
    if (mem == NULL) {
        if (error)
            *error = ENOMEM;
        return NULL;
    }

    // ensure its all zero!
    wasm_memory_fill(mem, 0, PAGE_SIZE);

    arena = (struct mm_arena *)mem;
    mem += sizeof(struct mm_arena);
    memsz = PAGE_SIZE - sizeof(struct mm_arena);

    mutex_init(&arena->malloc_lock, MUTEX_SPIN, 0);
    mutex_init(&arena->mstate.mutex, MUTEX_SPIN, 0);

    strlcpy(arena->mm_name, name, sizeof(arena->mm_name));

    arena->first_page = paddr_to_page(mem);
    arena->last_page = paddr_to_page(mem + memsz - 1);

    printf("%s init-mem-start: %p init-mem-end: %p\n", __func__, mem, mem + memsz);

    struct malloc_state *m = &arena->mstate;
    m->least_addr = mem;
    m->seg.base = mem;
    m->seg.size = memsz;
    m->seg.sflags = 0;
    m->magic = __mparams.magic;
    m->release_checks = 4095;
    init_bins(m);
    init_top(m, (struct malloc_chunk *)mem, memsz - TOP_FOOT_SIZE);

    return arena;
}

// addrp returns ptr to allocated memory
void *
mm_arena_alloc_simple(struct mm_arena *vm, size_t size, int *error)
{
    uintptr_t addr;
    int ret;
    if (size >= PAGE_SIZE) {
        wasm_kern_ioctl(537, (void *)size);
    }
    addr = 0;
    ret = mm_malloc_internal(vm, size, 0, &addr);
    if (error)
        *error = ret;

    return (void *)addr;
}

// addrp returns ptr to allocated memory
void *
mm_arena_zalloc_simple(struct mm_arena *vm, size_t size, int *error)
{
    uintptr_t addr;
    int ret;
    if (size >= PAGE_SIZE) {
        wasm_kern_ioctl(537, (void *)size);
    }
    addr = 0;
    ret = mm_malloc_internal(vm, size, 0, &addr);
    if (error)
        *error = ret;

    if (addr != 0) {
        wasm_memory_fill((void *)addr, 0, size);
    }

    return (void *)addr;
}

void
mm_arena_free_simple(struct mm_arena *vm, void * addr)
{
    mm_free_internal(vm, addr);
}

void mm_arena_destroy_simple(struct mm_arena *vm)
{
    uint32_t page_count;
    struct mm_page *first, *last;
    first = vm->first_page;
    last = vm->last_page;
    page_count = vm->page_count;
    vm->first_page = NULL;
    vm->last_page = NULL;
    vm->page_count = 0;

    kmem_page_free(first, page_count);
}


#if 0
int
uvm_km_kmem_alloc(vmem_t *vm, vmem_size_t size, vm_flag_t flags,
    vmem_addr_t *addr)
{
    struct mm_arena *mem = (struct mm_arena *)vm;
    mm_malloc_internal(mem, size, flags, addr);
}

void
uvm_km_kmem_free(vmem_t *vm, vmem_addr_t addr, size_t size)
{
    struct mm_arena *mem = (struct mm_arena *)vm;
    mm_free_internal(mem, (void *)addr);
}
#endif
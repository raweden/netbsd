
#include <sys/cdefs.h>
#include <sys/stdbool.h>
#include <sys/stdint.h>
#include <sys/errno.h>
#include <sys/null.h>
#include <stddef.h>

#include <arch/wasm/include/wasm/builtin.h>
#include <arch/wasm/include/wasm_inst.h>

#include <arch/wasm/libwasm/wasmloader.h>

#include "internal.h"

// unlike the mm.c implementation this is based entierly on the flags, a single u8 value is used to represent each page
// 


struct rtld_mmap_umem_info {
    int32_t initial;
    int32_t maximum;
    bool shared;
};

#define EXEC_IOCTL_UMEM_INFO 599

#define PGVEC_FL_UNMAPPED   (0xff) // mmap is not the owner of page chunk (memory.grow outside of mmap)
#define PGVEC_FL_FREE       (0x00)
#define PGVEC_FL_USED       (0x01)
#define PGVEC_FL_ANON       (0x02)

struct mmap_state {
    uint32_t mtx;
    uint32_t npg_free;      // number of PAGE_SIZE chunks which is marked as free.
    uint32_t npg_unmapped;  // number of PAGE_SIZE chunks which is marked as unmapped (memory.grow outside mmap)
    uintptr_t last_brk;
    uint32_t pgvec_cap;     // count of the number of pages mapped currently.
    uint32_t pgvec_min;     // minimum valid page index. (used or free, but not unmapped) 
    uint32_t pgvec_max;     // maximum valid page index. (used or free, but not unmapped)
    uint8_t *pgvec;
    int32_t mem_min;
    int32_t mem_max;
    bool mem_growable;
} mm_state;

int npagesizes = 1;
size_t mm_pagesizes[1] = {
    PAGE_SIZE
};

size_t *pagesizes = (size_t *)&mm_pagesizes;

#define WASM_PAGESZ_NPG (WASM_PAGE_SIZE / PAGE_SIZE)

extern struct rtld_memory_descriptor __rtld_memdesc;

int
__crt_mmap_init(void)
{
    uint32_t min, max, cur;
    uint32_t end, vecsz, cap, growsz, npg;     // number of wasm pages to hold pgvec
    int32_t growret, lastbrk;
    uint8_t *vec, *vec_p, *vec_e;

    min = __rtld_memdesc.initial;
    max = __rtld_memdesc.maximum;

    dbg("%s min = %d max = %d\n", __func__, min, max);

    cap = max * (WASM_PAGE_SIZE / PAGE_SIZE);
    growsz = howmany(cap, WASM_PAGE_SIZE);

    growret = wasm_memory_grow(growsz);
    if (growret == -1)
        return ENOMEM;

    vec = (void *)(growret * WASM_PAGE_SIZE);
    mm_state.pgvec = vec;

    npg = howmany(cap, PAGE_SIZE);
    vecsz = npg * PAGE_SIZE;

    dbg("%s vec = %p npg = %d vec-size = %d\n", __func__, vec, npg, vecsz);

    // zero fill the entire vector.
    wasm_memory_fill(vec, PGVEC_FL_UNMAPPED, vecsz);

    
    vec_p = vec + (growret * WASM_PAGESZ_NPG);
    wasm_memory_fill(vec_p, PGVEC_FL_USED, npg);

    dbg("%s marking addr %p pgi = %d npg = %d as used", __func__, vec_p, (int)(vec_p - vec), npg);

    lastbrk = (growret + growsz);
    mm_state.pgvec_cap = npg * PAGE_SIZE;
    mm_state.last_brk = lastbrk;
    mm_state.pgvec_max = lastbrk * WASM_PAGESZ_NPG;
    mm_state.npg_unmapped = growret * WASM_PAGESZ_NPG;
    __builtin_atomic_store32(&mm_state.pgvec_min, growret * WASM_PAGESZ_NPG);
    __builtin_atomic_store32(&mm_state.pgvec_max, lastbrk * WASM_PAGESZ_NPG);

    dbg("%s pgvec_cap = %d last_brk = %lu pgvec_min = %d pgvec_max = %d npg_unmapped = %d\n", __func__, mm_state.pgvec_cap, mm_state.last_brk, mm_state.pgvec_min, mm_state.pgvec_max, mm_state.npg_unmapped);

    if (npg != (growsz * WASM_PAGESZ_NPG)) {
        vec_p = vec_p + npg;
        npg = (growsz * WASM_PAGESZ_NPG) - npg;
        wasm_memory_fill(vec_p, PGVEC_FL_FREE, npg);
        mm_state.npg_free = npg;
        dbg("%s marking addr %p pgi = %d npg %d as free\n", __func__, vec_p, (int)(vec_p - mm_state.pgvec), npg);
    }

    return (0);
}

int
__crt_munmap(void *addr, size_t len)
{
    uint8_t *pv, *pe;
    uint32_t pgi, pge, npg;
    uint8_t val;
    int32_t err;

    if (len <= 0 || (len % PAGE_SIZE) != 0) {
        return EINVAL;
    }

    if (((uintptr_t)(addr) % PAGE_SIZE) != 0) {
        return EINVAL;
    }

    npg = howmany(len, PAGE_SIZE);
    pgi = (uintptr_t)addr >> PAGE_SHIFT;
    pge = pgi + npg;

    _rtld_mutex_enter(&mm_state.mtx);

    if (pgi < mm_state.pgvec_min || pge > mm_state.pgvec_max) {
        _rtld_mutex_exit(&mm_state.mtx);
        return EINVAL;
    }

    pv = mm_state.pgvec + pgi;
    pe = pv + npg;
    err = 0;

    while (pv < pe) {
        val = *(pv);
        if (val == PGVEC_FL_UNMAPPED || val == PGVEC_FL_FREE) {
            err = EINVAL;
            break;
        } else if ((val & PGVEC_FL_USED) == 0) {
            err = EINVAL;
            break;
        }
        pv++;
    }

    if (err != 0) {
        _rtld_mutex_exit(&mm_state.mtx);
        return err;
    }

    pv = mm_state.pgvec + pgi;
    wasm_memory_fill(pv, PGVEC_FL_FREE, npg);
    mm_state.npg_free += npg;
    dbg("%s marking addr %p pgi = %d npg %d as free\n", __func__, pv, pgi, npg);

    wasm_memory_fill(addr, 0, len);

    _rtld_mutex_exit(&mm_state.mtx);

    return (0);
}

static int32_t
__crt_mmap_find_free(uint8_t *pvec, int32_t start, int32_t end, size_t npg)
{
    uint8_t *pv_ptr, *pv_end;
    uint8_t val;
    uint32_t rem;

    pv_ptr = pvec + start;
    pv_end = pvec + end;

    while (pv_ptr < pv_end) {
        val = *(pv_ptr);
        if (val == PGVEC_FL_UNMAPPED) {
            rem = (uint32_t)(pv_ptr - pvec);
            if (rem == 0) {
                pv_ptr += WASM_PAGESZ_NPG;
            } else {
                rem = (rem % WASM_PAGESZ_NPG);
                pv_ptr += (WASM_PAGESZ_NPG - rem);
            }
        } else if (val == PGVEC_FL_FREE) {
            int32_t idx = pv_ptr - pvec;
            int32_t cnt = 1;
            pv_ptr++;
            while (pv_ptr < pv_end && cnt < npg) {
                val = *(pv_ptr);
                if (val == PGVEC_FL_UNMAPPED) {
                    rem = (uint32_t)(pv_ptr - pvec);
                    if (rem == 0) {
                        pv_ptr += WASM_PAGESZ_NPG;
                    } else {
                        rem = (rem % WASM_PAGESZ_NPG);
                        pv_ptr += (WASM_PAGESZ_NPG - rem);
                    }
                    cnt = -1;
                    break;
                } else if (val == PGVEC_FL_FREE) {
                    pv_ptr++;
                    cnt++;
                } else {
                    pv_ptr++;
                    cnt = -1;
                    break;
                }
            }

            if (cnt != -1) {
                if (cnt == npg) {
                    return idx;
                } else {
                    return -1;
                }
            }
                

        } else {
            pv_ptr++;
        }
    }


    return -1;
}

static int32_t
__crt_mmap_tail_free(void)
{
    return (0);
}

static bool
__crt_mmap_is_free(uint8_t *pvec, size_t npg)
{
    uint8_t *end = pvec + npg;

    while (pvec < end) {
        if (*(pvec) != PGVEC_FL_FREE) {
            return false;
        }
        pvec++;
    }

    return true;
}

/**
 * @param npg The number of `PAGE_SIZE` chunks to grow memory with.
 * @param flags The flags to be applied to the new memory region of `npg` size.
 * @return The index of the page measured in `PAGE_SIZE` chunks, or `-1` if memory could not be grown.
 */
static int32_t __noinline
__crt_mmap_grow_memory(uint32_t npg, uint8_t flags)
{
    uint8_t *pvec;
    int32_t growsz, growret, growbrk, lastbrk, cnt;
    uint32_t tailcnt, pgi;

    // TODO: optimize by checking tail for free pages
    // check for free pages in tail only if the last grow is mapped by mmap.
    tailcnt = 0;
    lastbrk = mm_state.last_brk;
    growbrk = wasm_memory_size();
    if (growbrk == lastbrk) {
        pvec = mm_state.pgvec;
        tailcnt = 0;
        for (int i = mm_state.pgvec_max - 1; i >= 0; i--) {
            if (pvec[i] == PGVEC_FL_FREE) {
                tailcnt++;
            } else {
                break;
            }
        }
        dbg("%s got %d free pages in tail\n", __func__, tailcnt);
    }

    if (tailcnt > 0) {
        cnt = npg - tailcnt;
    } else {
        cnt = npg;
    }

    growsz = howmany(cnt, WASM_PAGESZ_NPG);

    growret = wasm_memory_grow(growsz);
    if (growret == -1) {
        return ENOMEM;
    }

    pvec = mm_state.pgvec;

    if (growret != mm_state.last_brk) {
        growbrk = mm_state.last_brk;
        cnt = (growret - growbrk) * WASM_PAGESZ_NPG;
        growbrk = growbrk * WASM_PAGESZ_NPG;
        wasm_memory_fill(pvec + growbrk, PGVEC_FL_UNMAPPED, cnt);
        mm_state.npg_unmapped += cnt;
        dbg("%s marking addr %p pgi = %d npg %d as unmapped\n", __func__, pvec + growbrk, growbrk, cnt);
    }

    pgi = growret * WASM_PAGESZ_NPG;
    if (tailcnt != 0) {
        pgi = pgi - tailcnt;
    }

    wasm_memory_fill(pvec + pgi, flags, npg);
    dbg("%s marking addr %p pgi = %d npg %d as used (flags = %d)\n", __func__, pvec + pgi, pgi, npg, flags);

    lastbrk = growret + growsz;
    mm_state.last_brk = lastbrk;
    __builtin_atomic_store32(&mm_state.pgvec_max, lastbrk * WASM_PAGESZ_NPG);
    dbg("%s last_brk is now %d (%d)\n", __func__, lastbrk, lastbrk * WASM_PAGESZ_NPG);
    cnt = (lastbrk * WASM_PAGESZ_NPG) - (pgi + npg);
    if (cnt > 0) {
        lastbrk = (lastbrk * WASM_PAGESZ_NPG);
        wasm_memory_fill(pvec + (pgi + npg), PGVEC_FL_FREE, cnt);
        mm_state.npg_free += cnt;
        dbg("%s marking addr %p pgi = %d npg %d as free\n", __func__, pvec + (pgi - npg), (pgi + npg), cnt);
    } else if (cnt < 0) {
        dbg("%s count after marking is negative %d\n", __func__, cnt);
    }

    return pgi;
}

/**
 * like `mmap()` but only supports `MAP_ANON`
 */
void *
__crt_mmap(void *addr, size_t len, int prot, int flags, int fd, int64_t offset, int *error)
{
    struct mm_page *pg;
    uintptr_t pgv;
    uint32_t pgi, npg, pflag, growsz;
    uint8_t *pvec, *pe;
    int err, growret;

    _rtld_mutex_enter(&mm_state.mtx);

    pvec = mm_state.pgvec;
    err = 0;

    if (((flags & MAP_ANON) != 0) && fd != -1 && offset != 0) {
        err = EINVAL;
        goto error_out;
    }

    npg = howmany(len, PAGE_SIZE);

    if (addr != NULL) {

        if (((uintptr_t)(addr) % PAGE_SIZE) != 0) {
            err = EINVAL;
            goto error_out;
        }

        pgi = (uintptr_t)(addr) >> PAGE_SHIFT;
        pe = pvec + pgi;
        if (__crt_mmap_is_free(pe, npg)) {

            wasm_memory_fill(pe, (PGVEC_FL_USED | PGVEC_FL_ANON), npg);
            mm_state.npg_free -= npg;
            dbg("%s marking addr %p pgi = %d npg %d as used (from __crt_mmap_is_free)\n", __func__, pe, pgi, npg);

            _rtld_mutex_exit(&mm_state.mtx);

            return (void *)(pgi * PAGE_SIZE);
        }
    }

    // if more than total go strait to growing.
    if (npg > mm_state.npg_free) {
        growret = __crt_mmap_grow_memory(npg, PGVEC_FL_USED|PGVEC_FL_ANON);
        if (growret == -1) {
            err = ENOMEM;
            goto error_out;
        }

        _rtld_mutex_exit(&mm_state.mtx);

        return (void *)(growret * PAGE_SIZE);
    }

    pgi = __crt_mmap_find_free(pvec, 1, mm_state.pgvec_max, npg);
    if (pgi != -1) {

        wasm_memory_fill(pvec + pgi, (PGVEC_FL_USED | PGVEC_FL_ANON), npg);
        mm_state.npg_free -= npg;
        dbg("%s marking addr %p pgi = %d npg %d as used (from __crt_mmap_find_free)\n", __func__, pvec + pgi, pgi, npg);
        
        mm_state.npg_free -= npg;

        _rtld_mutex_exit(&mm_state.mtx);

        return (void *)(pgi * PAGE_SIZE);
    }
    
    growret = __crt_mmap_grow_memory(npg, PGVEC_FL_USED|PGVEC_FL_ANON);
    if (growret == -1) {
        err = ENOMEM;
        goto error_out;
    }

    _rtld_mutex_exit(&mm_state.mtx);

    return (void *)(growret * PAGE_SIZE);

error_out:

    _rtld_mutex_exit(&mm_state.mtx);

    if (error)
        *error = err;

    return (MAP_FAILED);
}
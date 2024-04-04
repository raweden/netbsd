
#include <sys/cdefs.h>
#include <sys/stdint.h>
#include <sys/errno.h>
#include <sys/null.h>
#include <stddef.h>

#include <arch/wasm/include/wasm/builtin.h>
#include <arch/wasm/include/wasm_inst.h>

#include "internal.h"

// unlike the mm.c implementation this is based entierly on the flags, a single u8 value is used to represent each page
// 


struct rtld_mmap_umem_info {
    int32_t initial;
    int32_t maximum;
    bool shared;
};

#define EXEC_IOCTL_UMEM_INFO 599

#undef PAGE_SIZE
#undef PAGE_SHIFT
#define PAGE_SIZE 4096
#define PAGE_SHIFT 13

#define WASM_PAGE_SIZE 65536

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

int
__crt_mmap_init(void)
{
    struct rtld_mmap_umem_info mem_info;
    uint32_t min, max, cur;
    uint32_t end, vecsz, cap, growsz, npg;     // number of wasm pages to hold pgvec
    int32_t growret, lastbrk;
    uint8_t *vec, *vec_p, *vec_e;

    mem_info.initial = 0;
    mem_info.maximum = 0;
    mem_info.shared = false;
    growret = _rtld_exec_ioctl(EXEC_IOCTL_UMEM_INFO, &mem_info);

    min = mem_info.initial;
    max = mem_info.maximum;

    cap = max * 32;
    growsz = howmany(cap, WASM_PAGE_SIZE);

    growret = wasm_memory_grow(growsz);
    if (growret == -1)
        return ENOMEM;

    vec = (void *)(growret * WASM_PAGE_SIZE);
    mm_state.pgvec = vec;

    // zero fill the entire vector.
    wasm_memory_fill(vec, PGVEC_FL_UNMAPPED, vecsz * WASM_PAGE_SIZE);

    npg = howmany(cap, PAGE_SIZE);
    vec_p = vec + (growret * 32);
    wasm_memory_fill(vec_p, PGVEC_FL_USED, npg);

    lastbrk = (growret + growsz);
    mm_state.pgvec_cap = npg * PAGE_SIZE;
    mm_state.last_brk = lastbrk;
    mm_state.pgvec_max = lastbrk * 32;
    mm_state.npg_unmapped = growret * 32;
    __builtin_atomic_store32(&mm_state.pgvec_min, growret * 32);
    __builtin_atomic_store32(&mm_state.pgvec_max, lastbrk * 32);


    if (npg != (growsz * 32)) {
        vec_p = vec_p + npg;
        npg = (growsz * 32) - npg;
        wasm_memory_fill(vec_p, PGVEC_FL_FREE, npg);
        mm_state.npg_free = npg;
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

    _rtld_mutex_exit(&mm_state.mtx);

    return (0);
}

static int32_t
__crt_mmap_find_free(uint8_t *pvec, int32_t start, int32_t end, size_t npg)
{
    uint8_t *pv_ptr, *pv_end;
    uint8_t val;

    pv_ptr = pvec + start;
    pv_end = pvec + end;

    while (pv_ptr < pv_end) {
        val = *(pv_ptr);
        if (val == PGVEC_FL_UNMAPPED) {
            if (((uintptr_t)(pv_ptr) % WASM_PAGE_SIZE) == 0) {
                pv_ptr += 32;
            } else {
                pv_ptr++;
            }
        } else if (val == PGVEC_FL_FREE) {
            int32_t idx = pv_ptr - pvec;
            int32_t cnt = 1;
             pv_ptr++;
            while (pv_ptr < pv_end && cnt < npg) {
                val = *(pv_ptr);
                if (val == PGVEC_FL_UNMAPPED) {
                    if (((uintptr_t)(pv_ptr) % WASM_PAGE_SIZE) == 0) {
                        pv_ptr += 32;
                        cnt = -1;
                    } else {
                        pv_ptr++;
                        cnt = -1;
                    }
                    break;
                } else if (val != PGVEC_FL_FREE) {
                    pv_ptr++;
                    cnt = -1;
                    break;
                } else {
                    pv_ptr++;
                    cnt++;
                }
            }

            if (cnt != -1)
                return idx;

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
    }

    return true;
}

/**
 * @param npg The number of `PAGE_SIZE` chunks to grow memory with.
 * @param flags The flags to be applied to the new memory region of `npg` size.
 * @return The index of the page measured in `PAGE_SIZE` chunks, or `-1` if memory could not be grown.
 */
static int32_t
__crt_mmap_grow_memory(uint32_t npg, uint8_t flags)
{
    uint8_t *pvec;
    int32_t growsz, growret, growbrk, lastbrk, cnt;

    growsz = howmany(npg, 32);

    growret = wasm_memory_grow(growsz);
    if (growret == -1) {
        return ENOMEM;
    }

    pvec = mm_state.pgvec;

    if (growret != mm_state.last_brk) {
        growbrk = mm_state.last_brk;
        cnt = (growret - growbrk) * 32;
        growbrk = growbrk * 32;
        wasm_memory_fill(pvec + growbrk, PGVEC_FL_UNMAPPED, cnt);
        mm_state.npg_unmapped += cnt;
    }

    growbrk = growret * 32;

    wasm_memory_fill(pvec + growbrk, flags, npg);

    lastbrk = growret + growsz;
    mm_state.last_brk = lastbrk;
    cnt = growsz * 32;
    if (cnt != npg) {
        cnt = cnt - npg;
        lastbrk = (lastbrk * 32) - cnt;
        wasm_memory_fill(pvec + lastbrk, PGVEC_FL_FREE, cnt);
        mm_state.npg_free += cnt;
    }

    __builtin_atomic_store32(&mm_state.pgvec_max, lastbrk * 32);

    return growbrk;
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

            _rtld_mutex_exit(&mm_state.mtx);

            return (void *)(pgi * PAGE_SIZE);
        }
    }

    npg = howmany(len, PAGE_SIZE);

    pgi = __crt_mmap_find_free(pvec, 1, mm_state.pgvec_max, npg);
    if (pgi != -1) {
        
        mm_state.npg_free -= npg;

        _rtld_mutex_exit(&mm_state.mtx);

        return (void *)(pgi * PAGE_SIZE);
    }
    
    growret = __crt_mmap_grow_memory(npg, PGVEC_FL_USED|PGVEC_FL_ANON);
    if (growret == -1) {
        err = ENOMEM;
        goto error_out;
    }

    return (void *)(growret * PAGE_SIZE);

error_out:

    _rtld_mutex_exit(&mm_state.mtx);

    if (error)
        *error = err;

    return (MAP_FAILED);
}
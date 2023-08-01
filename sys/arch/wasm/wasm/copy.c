
#include <sys/cdefs.h>
__RCSID("$NetBSD: copy.c,v 1.21 2023/05/07 12:41:49 skrll Exp $");

#include <sys/types.h>
#include <sys/param.h>

#include <wasm/wasm_module.h>

// calls into hosting JavaScript land as we cannot copy from two origin with the current spec.
int fetch_user_data(const void *uaddr, void *valp, size_t size) __WASM_IMPORT(kern, __fetch_user_data);
int store_user_data(void *uaddr, const void *valp, size_t size) __WASM_IMPORT(kern, __store_user_data);
int __ucas32(volatile uint32_t *, uint32_t, uint32_t, uint32_t *) __WASM_IMPORT(kern, ucas32);
int __copyargs(void *, void *, size_t) __WASM_IMPORT(kern, copyargs);


/*
 * Compare-and-swap the 32-bit integer in the user-space.
 */
int
_ucas_32(volatile uint32_t *uptr, uint32_t old, uint32_t new, uint32_t *ret)
{
    int err;
    err = __ucas32(uptr, old, new, ret);
    return (err);
}


int
_ufetch_8(const uint8_t *uaddr, uint8_t *valp)
{
	return fetch_user_data(uaddr, valp, sizeof(*valp));
}

int
_ufetch_16(const uint16_t *uaddr, uint16_t *valp)
{
	return fetch_user_data(uaddr, valp, sizeof(*valp));
}

int
_ufetch_32(const uint32_t *uaddr, uint32_t *valp)
{
	return fetch_user_data(uaddr, valp, sizeof(*valp));
}

#ifdef _LP64
int
_ufetch_64(const uint64_t *uaddr, uint64_t *valp)
{
	return fetch_user_data(uaddr, valp, sizeof(*valp));
}
#endif /* _LP64 */

int
_ustore_8(uint8_t *uaddr, uint8_t val)
{
	return store_user_data(uaddr, &val, sizeof(val));
}

int
_ustore_16(uint16_t *uaddr, uint16_t val)
{
	return store_user_data(uaddr, &val, sizeof(val));
}

int
_ustore_32(uint32_t *uaddr, uint32_t val)
{
	return store_user_data(uaddr, &val, sizeof(val));
}

#ifdef _LP64
int
_ustore_64(uint64_t *uaddr, uint64_t val)
{
	return store_user_data(uaddr, &val, sizeof(val));
}
#endif /* _LP64 */
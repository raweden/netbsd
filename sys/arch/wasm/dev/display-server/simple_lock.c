
#include <sys/cdefs.h>

#include <sys/mutex.h>
#include <sys/proc.h>

#include <wasm/wasm_module.h>
#include <wasm/wasm_inst.h>
#include <wasm/wasm-extra.h>

bool
simple_lock_owned(kmutex_t *mtx)
{
	return __builtin_atomic_load32((uint32_t *)mtx) == (uint32_t)curlwp;
}

bool
simple_trylock(kmutex_t *mtx)
{
	uint32_t old;
	old = __builtin_atomic_rmw_cmpxchg32((uint32_t *)mtx, 0, (uint32_t)curlwp);
	return old == 0;
}

/*
 * void mutex_spin_enter(kmutex_t *mtx);
 *
 * Acquire a spin mutex and post a load fence.
 */
void
simple_lock(kmutex_t *mtx)
{
	uint32_t old, cnt, clwp;
	cnt = 0;
	clwp = (uint32_t)curlwp;

	old = __builtin_atomic_rmw_cmpxchg32((uint32_t *)mtx, 0, clwp);
	if (old == 0 || old == clwp)
		return;

	while (cnt < 10000) {
		old = __builtin_atomic_rmw_cmpxchg32((uint32_t *)mtx, 0, clwp);
		if (old == 0)
			return;	
	}

	printf("failed to acquire lock at %p value = %d", mtx, old);
	__panic_abort();
}

/*
 * Release a spin mutex and post a store fence. Must occupy 128 bytes.
 */
void
simple_unlock(kmutex_t *mtx)
{
	uint32_t old, clwp;
	clwp = (uint32_t)curlwp;

	old = __builtin_atomic_rmw_cmpxchg32((uint32_t *)mtx, clwp, 0);
	if (old == 0 || old == clwp)
		return;

	printf("old spin-lock value is not of expected value. Expected %d found %d\n", clwp, old);
	__panic_abort();
}

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: uvm_mmap.c,v 1.184 2022/07/07 11:29:18 rin Exp $");

#include "opt_compat_netbsd.h"
#include "opt_pax.h"

#include <sys/param.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/resourcevar.h>
#include <sys/mman.h>
#include <sys/pax.h>

#include <sys/errno.h>
#include <sys/syscallargs.h>

#include <wasm/wasm_module.h>

void __panic_abort(void) __WASM_IMPORT(kern, panic_abort);

// this source contains syscalls which is dependent upon virtual memory mapping in a global domain

int
sys___msync13(struct lwp *l, const struct sys___msync13_args *uap, register_t *retval)
{
    __panic_abort();
    return (ENOSYS);
}

int
sys_madvise(struct lwp *l, const struct sys_madvise_args *uap, register_t *retval)
{
    __panic_abort();
    return (ENOSYS);
}

int
sys_mincore(struct lwp *l, const struct sys_mincore_args *uap, register_t *retval)
{
    __panic_abort();
    return (ENOSYS);
}

int
sys_minherit(struct lwp *l, const struct sys_minherit_args *uap, register_t *retval)
{
    __panic_abort();
    return (ENOSYS);
}

int
sys_mlock(struct lwp *l, const struct sys_mlock_args *uap, register_t *retval)
{
    __panic_abort();
    return (ENOSYS);
}

int
sys_mlockall(struct lwp *l, const struct sys_mlockall_args *uap, register_t *retval)
{
    __panic_abort();
    return (ENOSYS);
}

int
sys_mmap(struct lwp *l, const struct sys_mmap_args *uap, register_t *retval)
{
    __panic_abort();
    return (ENOSYS);
}

int
sys_mprotect(struct lwp *l, const struct sys_mprotect_args *uap, register_t *retval)
{
    __panic_abort();
    return (ENOSYS);
}

int
sys_mremap(struct lwp *l, const struct sys_mremap_args *uap, register_t *retval)
{
    __panic_abort();
    return (ENOSYS);
}

int
sys_munlock(struct lwp *l, const struct sys_munlock_args *uap, register_t *retval)
{
    __panic_abort();
    return (ENOSYS);
}

int
sys_munlockall(struct lwp *l, const void *v, register_t *retval)
{
    __panic_abort();
    return (ENOSYS);
}

int
sys_munmap(struct lwp *l, const struct sys_munmap_args *uap, register_t *retval)
{
    __panic_abort();
    return (ENOSYS);
}

int
sys_obreak(struct lwp *l, const struct sys_obreak_args *uap, register_t *retval)
{
    __panic_abort();
    return (ENOSYS);
}

int
sys_ovadvise(struct lwp *l, const struct sys_ovadvise_args *uap, register_t *retval)
{
    __panic_abort();
    return (ENOSYS);
}

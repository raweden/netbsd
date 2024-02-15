#include "errno.h"
#include <sys/syscallargs.h>

#include <uvm/uvm_extern.h>

/*
 * These get filled in if/when SYSVSHM shared memory code is loaded
 *
 * We do this with function pointers rather the #ifdef SYSVSHM so the
 * SYSVSHM code can be loaded and unloaded
 */
void (*uvm_shmexit)(struct vmspace *) = NULL;
void (*uvm_shmfork)(struct vmspace *, struct vmspace *) = NULL;


/*
 * sys_mremap: mremap system call.
 */

int
sys_mremap(struct lwp *l, const struct sys_mremap_args *uap, register_t *retval)
{
#if 0
	/* {
		syscallarg(void *) old_address;
		syscallarg(size_t) old_size;
		syscallarg(void *) new_address;
		syscallarg(size_t) new_size;
		syscallarg(int) flags;
	} */

	struct proc *p;
	struct vm_map *map;
	vaddr_t oldva;
	vaddr_t newva;
	size_t oldsize;
	size_t newsize;
	int flags;
	int error;

	flags = SCARG(uap, flags);
	oldva = (vaddr_t)SCARG(uap, old_address);
	oldsize = (vsize_t)(SCARG(uap, old_size));
	newva = (vaddr_t)SCARG(uap, new_address);
	newsize = (vsize_t)(SCARG(uap, new_size));

	if ((flags & ~(MAP_FIXED | MAP_REMAPDUP | MAP_ALIGNMENT_MASK)) != 0) {
		error = EINVAL;
		goto done;
	}

	oldsize = round_page(oldsize);
	newsize = round_page(newsize);

	p = l->l_proc;
	map = &p->p_vmspace->vm_map;
	error = uvm_mremap(map, oldva, oldsize, map, &newva, newsize, p, flags);

done:
	*retval = (error != 0) ? 0 : (register_t)newva;
	return error;
#endif
    return ENOSYS;
}
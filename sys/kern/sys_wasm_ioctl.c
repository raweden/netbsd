

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: sys_memfd.c,v 1.2 2023/07/10 15:49:18 christos Exp $");

#include <sys/param.h>
#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/mman.h>
#include <sys/miscfd.h>
#include <sys/syscallargs.h>

#include <uvm/uvm_extern.h>
#include <uvm/uvm_object.h>

#if 0
	{
		ns(struct sys_wasm_ioctrl_args),
		.sy_flags = SYCALL_ARG_PTR,
		.sy_call = (sy_call_t *)sys_wasm_ioctrl
	},		/* 499 = __wasm_ioctrl */
#endif

/*
 * __wasm_ioctrl.
 */
int
sys_wasm_ioctrl(struct lwp *l, const struct sys_wasm_ioctrl_args *uap, register_t *retval)
{
	/* {
        syscallarg(int) cmd;
	    syscallarg(void *) arg;
    }
    555: WASM_IOCTL_GET_GUI_STREAM
    {
        syscallarg(const void *) hdr;
        syscallarg(const char *) session_id;
        syscallarg(unsigned int) flags;
	} */
	int error;

	error = 0;

	return 0;

leave:
	return error;
}

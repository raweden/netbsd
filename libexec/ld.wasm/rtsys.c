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


#include "rtsys.h"

struct wasm_trapframe { 	// 280 bytes
    uint64_t tf_ra;
    uint64_t tf_sp;
    uint64_t tf_gp;
    uint64_t tf_tp;
    uint64_t tf_t[7];
    uint64_t tf_s[12];
    uint64_t tf_a[8];
    uint64_t tf_sepc;
    uint64_t tf_sstatus;
    uint64_t tf_stval;
    uint64_t tf_scause;
};

#ifndef __WASM_IMPORT
#define __WASM_IMPORT(module, symbol) __attribute__((import_module(#module), import_name(#symbol)))
#endif

#ifndef PSL_C
#define PSL_C 0x00000001
#endif

void syscall_trap(struct wasm_trapframe *tf) __WASM_IMPORT(sys, syscall_trap);

extern int errno;

int 
__sys_open(const char *path, int flags, long arg)
{
	struct wasm_trapframe td_frame;
	td_frame.tf_t[0] = 5;
	td_frame.tf_a[0] = (uintptr_t)path;
	td_frame.tf_a[1] = flags;
	td_frame.tf_a[2] = (arg != 0 ? *((long *)(arg)) : 0);
	syscall_trap(&td_frame);
	if ((td_frame.tf_t[0] & PSL_C) != 0) {
		errno = td_frame.tf_a[0];
		return -1;
	} else {
		return (int)td_frame.tf_a[0];
	}
}

int 
__sys_close(int fd)
{
	struct wasm_trapframe td_frame;
	td_frame.tf_t[0] = 6;
	td_frame.tf_a[0] = fd;
	syscall_trap(&td_frame);
	if ((td_frame.tf_t[0] & PSL_C) != 0) {
		errno = td_frame.tf_a[0];
		return -1;
	} else {
		return (int)td_frame.tf_a[0];
	}
}

off_t 
__sys_lseek(int fd, off_t offset, int whence)
{
	struct wasm_trapframe td_frame;
	td_frame.tf_t[0] = 199;
	td_frame.tf_a[0] = fd;
	td_frame.tf_a[1] = offset;
	td_frame.tf_a[2] = whence;
	syscall_trap(&td_frame);
	if ((td_frame.tf_t[0] & PSL_C) != 0) {
		errno = td_frame.tf_a[0];
		return (off_t)(-1);
	} else {
		return (off_t)td_frame.tf_a[0];
	}
}

int 
__sys_getdents(int fd, char *buf, size_t count)
{
	struct wasm_trapframe td_frame;
	td_frame.tf_t[0] = 390;
	td_frame.tf_a[0] = fd;
	td_frame.tf_a[1] = (uintptr_t)buf;
	td_frame.tf_a[2] = count;
	syscall_trap(&td_frame);
	if ((td_frame.tf_t[0] & PSL_C) != 0) {
		errno = td_frame.tf_a[0];
		return -1;
	} else {
		return (int)td_frame.tf_a[0];
	}
}

ssize_t 
__sys_read(int fd, void *buf, size_t nbyte)
{
	struct wasm_trapframe td_frame;
	td_frame.tf_t[0] = 3;
	td_frame.tf_a[0] = fd;
	td_frame.tf_a[1] = (uintptr_t)buf;
	td_frame.tf_a[2] = nbyte;
	syscall_trap(&td_frame);
	if ((td_frame.tf_t[0] & PSL_C) != 0) {
		errno = td_frame.tf_a[0];
		return (ssize_t)(-1);
	} else {
		return (ssize_t)td_frame.tf_a[0];
	}
}

ssize_t 
__sys_write(int fd, const void *buf, size_t nbyte)
{
	struct wasm_trapframe td_frame;
	td_frame.tf_t[0] = 4;
	td_frame.tf_a[0] = fd;
	td_frame.tf_a[1] = (uintptr_t)buf;
	td_frame.tf_a[2] = nbyte;
	syscall_trap(&td_frame);
	if ((td_frame.tf_t[0] & PSL_C) != 0) {
		errno = td_frame.tf_a[0];
		return (ssize_t)(-1);
	} else {
		return (ssize_t)td_frame.tf_a[0];
	}
}

int 
__sys_fstat(int fd, struct stat *sb)
{
	struct wasm_trapframe td_frame;
	td_frame.tf_t[0] = 440;
	td_frame.tf_a[0] = fd;
	td_frame.tf_a[1] = (uintptr_t)sb;
	syscall_trap(&td_frame);
	if ((td_frame.tf_t[0] & PSL_C) != 0) {
		errno = td_frame.tf_a[0];
		return -1;
	} else {
		return (int)td_frame.tf_a[0];
	}
}

int 
__sys_lstat(const char *path, struct stat *ub)
{
	struct wasm_trapframe td_frame;
	td_frame.tf_t[0] = 441;
	td_frame.tf_a[0] = (uintptr_t)path;
	td_frame.tf_a[1] = (uintptr_t)ub;
	syscall_trap(&td_frame);
	if ((td_frame.tf_t[0] & PSL_C) != 0) {
		errno = td_frame.tf_a[0];
		return -1;
	} else {
		return (int)td_frame.tf_a[0];
	}
}

ssize_t 
__sys_readlink(const char *path, char *buf, size_t count)
{
	struct wasm_trapframe td_frame;
	td_frame.tf_t[0] = 58;
	td_frame.tf_a[0] = (uintptr_t)path;
	td_frame.tf_a[1] = (uintptr_t)buf;
	td_frame.tf_a[2] = count;
	syscall_trap(&td_frame);
	if ((td_frame.tf_t[0] & PSL_C) != 0) {
		errno = td_frame.tf_a[0];
		return (ssize_t)(-1);
	} else {
		return (ssize_t)td_frame.tf_a[0];
	}
}

int 
__sys_fcntl(int fd, int cmd, long arg)
{
	struct wasm_trapframe td_frame;
	td_frame.tf_t[0] = 92;
	td_frame.tf_a[0] = fd;
	td_frame.tf_a[1] = cmd;
	td_frame.tf_a[2] = arg;
	syscall_trap(&td_frame);
	if ((td_frame.tf_t[0] & PSL_C) != 0) {
		errno = td_frame.tf_a[0];
		return -1;
	} else {
		return (int)td_frame.tf_a[0];
	}
}
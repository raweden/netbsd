/*	$NetBSD: machdep.h,v 1.5 2023/06/12 19:04:14 skrll Exp $	*/

/*-
 * Copyright (c) 2022 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Nick Hudson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _WASM_MACHDEP_H_
#define _WASM_MACHDEP_H_

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: machdep.h,v 1.5 2023/06/12 19:04:14 skrll Exp $");

#include <sys/kcore.h>

extern phys_ram_seg_t mem_clusters[];
extern int mem_cluster_cnt;
extern vaddr_t msgbuf_vaddr;
extern unsigned int msgbuf_p_cnt;


struct btinfo_memmap;
struct extent;
struct sysctllog;

struct msgbuf_p_seg {
	paddr_t paddr;
	psize_t sz;
};

extern struct msgbuf_p_seg msgbuf_p_seg[];

void x86_cpu_idle_init(void);
void x86_cpu_idle_get(void (**)(void), char *, size_t);
void x86_cpu_idle_set(void (*)(void), const char *, bool);

extern u_long x86_rtclock_tval;
extern void (*x86_initclock_func)(void);

void x86_dummy_initclock(void);

int	x86_select_freelist(uint64_t);

void init_x86_clusters(void);
int	init_x86_vm(paddr_t);
void init_x86_msgbuf(void);

void x86_startup(void);
void x86_sysctl_machdep_setup(struct sysctllog **);

void x86_rndseed(void);

// below is riscv TODO: remove

#include <sys/proc.h>
#include <sys/lwp.h>
#include <sys/siginfo.h>

static inline paddr_t
riscv_kern_vtophys(vaddr_t va)
{
	extern unsigned long kern_vtopdiff;

	return va - kern_vtopdiff;
}

static inline vaddr_t
riscv_kern_phystov(paddr_t pa)
{
	extern unsigned long kern_vtopdiff;

	return pa + kern_vtopdiff;
}

#define KERN_VTOPHYS(va)	riscv_kern_vtophys((vaddr_t)va)
#define KERN_PHYSTOV(pa)	riscv_kern_phystov((paddr_t)pa)

extern	paddr_t physical_start;
extern	paddr_t physical_end;

void	uartputc(int);
int	uartgetc(void);

paddr_t	init_mmu(paddr_t);
void	init_wasm32(paddr_t);

void	riscv_timer_frequency_set(uint32_t);	// which header?
uint32_t
	riscv_timer_frequency_get(void);	// which header?
void	riscv_timer_register(void (*timerfn)(void));
void	riscv_intr_set_handler(void (*intr_handler)(struct trapframe *,
	    register_t, register_t, register_t));

void	riscv_timer_init(void);
int	riscv_timer_intr(void *arg);

void    pt_dump(void (*)(const char *, ...));


#endif	/* _WASM_MACHDEP_H_ */

/*	$NetBSD: stubs.c,v 1.2 2020/11/04 07:09:46 skrll Exp $	*/

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas of 3am Software Foundry.
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

#include <sys/cdefs.h>
__RCSID("$NetBSD: stubs.c,v 1.2 2020/11/04 07:09:46 skrll Exp $");

#include <sys/types.h>
#include <sys/null.h>
#include <sys/param.h>

#include <machine/bus_defs.h>
#include <machine/frame.h>

#include <dev/pci/pcivar.h>

//typedef uint32_t pcireg_t;

int x86_zeroop(void);
void *x86_nullop(void);
void x86_voidop(void);

void
x86_voidop(void)
{
}

void *
x86_nullop(void)
{
	return NULL;
}

int
x86_zeroop(void)
{
	return 0;
}

void voidop_vii(device_t dev, void *data)
{
	// nothing
}

device_t nullop_iii(device_t dev, void *data)
{
	return NULL;
}

int zeroop_iiii(int a1, int a2, struct trapframe * a3)
{
	return 0;
}

void noop_pci_ranges_infer(int a1, int a2, int a3, int a4, int a5, int a6, int a7)
{

}



void device_acpi_register(device_t, void *) __attribute__((weak, alias("voidop_vii")));
device_t device_isa_register(device_t, void *) __attribute__((weak, alias("nullop_iii")));
void device_pci_props_register(device_t, void *) __attribute__((weak, alias("voidop_vii")));
device_t device_pci_register(device_t, void *) __attribute__((weak, alias("nullop_iii")));
int kdb_trap(int, int, struct trapframe *) __attribute__((weak, alias("zeroop_iiii")));
void kgdb_disconnected(void) __attribute__((weak, alias("x86_zeroop")));
void kgdb_trap(void) __attribute__((weak, alias("x86_zeroop")));
void mca_nmi(void) __attribute__((weak, alias("x86_voidop")));
void pci_ranges_infer(pci_chipset_tag_t, int, int, bus_addr_t *, bus_size_t *, bus_addr_t *, bus_size_t *) __attribute__((weak, alias("noop_pci_ranges_infer")));
void x86_nmi(void) __attribute__((weak, alias("x86_voidop")));
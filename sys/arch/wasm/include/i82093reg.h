/*	 $NetBSD: i82093reg.h,v 1.11 2017/11/13 11:45:54 nakayama Exp $ */

/* 	$NetBSD: i82093reg.h,v 1.7 2022/10/06 06:51:36 msaitoh Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by RedBack Networks Inc.
 *
 * Author: Bill Sommerfeld
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

/*
 * Typically, the first apic lives here.
 */
#define IOAPIC_BASE_DEFAULT	0xfec00000

/*
 * Memory-space registers.
 */

/*
 * The externally visible registers are all 32 bits wide;
 * store the register number of interest in IOAPIC_REG, and store/fetch
 * the real value in IOAPIC_DATA.
 */
   


#define	IOAPIC_REG		0x0000
#define IOAPIC_DATA		0x0010
#define IOAPIC_EOI		0x0040
#define		IOAPIC_EOI_MASK		0x000000ff

/*
 * Internal I/O APIC registers.
 */

#define IOAPIC_ID		0x00

#define 	IOAPIC_ID_SHIFT		24
#define		IOAPIC_ID_MASK		0xff000000

/* Version, and maximum interrupt pin number. */
  
#define IOAPIC_VER		0x01

#define		IOAPIC_VER_SHIFT		0
#define		IOAPIC_VER_MASK			0x000000ff

#define		IOAPIC_MAX_SHIFT	       	16
#define		IOAPIC_MAX_MASK	       	0x00ff0000

/*
 * Arbitration ID.  Same format as IOAPIC_ID register.
 */
#define IOAPIC_ARB		0x02

/*
 * Redirection table registers.
 */

#define IOAPIC_REDTBL		0x10
#define IOAPIC_REDHI(pin)	(IOAPIC_REDTBL + ((pin) << 1) + 1)
#define IOAPIC_REDLO(pin)	(IOAPIC_REDTBL + ((pin) << 1))

#define IOAPIC_REDHI_DEST_SHIFT		24	   /* destination. */
#define IOAPIC_REDHI_DEST_MASK		0xff000000

#define IOAPIC_REDLO_MASK		0x00010000 /* 0=enabled; 1=masked */

#define IOAPIC_REDLO_LEVEL		0x00008000 /* 0=edge, 1=level */
#define IOAPIC_REDLO_RIRR		0x00004000 /* remote IRR; read only */
#define IOAPIC_REDLO_ACTLO		0x00002000 /* 0=act. hi; 1=act. lo */
#define IOAPIC_REDLO_DELSTS		0x00001000 /* 0=idle; 1=send pending */
#define IOAPIC_REDLO_DSTMOD		0x00000800 /* 0=physical; 1=logical */

#define IOAPIC_REDLO_DEL_MASK		0x00000700 /* del. mode mask */
#define IOAPIC_REDLO_DEL_SHIFT		8

#define IOAPIC_REDLO_DEL_FIXED		0
#define IOAPIC_REDLO_DEL_LOPRI		1
#define IOAPIC_REDLO_DEL_SMI		2
#define IOAPIC_REDLO_DEL_NMI		4
#define IOAPIC_REDLO_DEL_INIT		5
#define IOAPIC_REDLO_DEL_EXTINT		7

#define IOAPIC_REDLO_VECTOR_MASK	0x000000ff /* delivery vector */

#define IMCR_ADDR		0x22
#define IMCR_DATA		0x23

#define IMCR_REGISTER		0x70
#define		IMCR_PIC	0x00
#define 	IMCR_APIC	0x01


#ifdef _KERNEL

#if defined(_KERNEL_OPT)
#include "opt_multiprocessor.h"
#endif

#define ioapic_asm_ack(num) \
	movl	_C_LABEL(local_apic_va),%eax	; \
	movl	$0,LAPIC_EOI(%eax)

#define x2apic_asm_ack(num) \
	movl	$(MSR_X2APIC_BASE + MSR_X2APIC_EOI),%ecx ; \
	xorl	%eax,%eax			; \
	xorl	%edx,%edx			; \
	wrmsr

#ifdef MULTIPROCESSOR

#define ioapic_asm_lock(num) \
	movl	$1,%esi						;\
77:								\
	xchgl	%esi,PIC_LOCK(%edi)				;\
	testl	%esi,%esi					;\
	jne	77b

#define ioapic_asm_unlock(num) \
	movl	$0,PIC_LOCK(%edi)
	
#else

#define ioapic_asm_lock(num)
#define ioapic_asm_unlock(num)

#endif	/* MULTIPROCESSOR */

#define ioapic_mask(num) \
	movl	IS_PIC(%ebp),%edi				;\
	ioapic_asm_lock(num)					;\
	movl	IS_PIN(%ebp),%esi				;\
	leal	0x10(%esi,%esi,1),%esi				;\
	movl	PIC_IOAPIC(%edi),%edi				;\
	movl	IOAPIC_SC_REG(%edi),%ebx			;\
	movl	%esi, (%ebx)					;\
	movl	IOAPIC_SC_DATA(%edi),%ebx			;\
	movl	(%ebx),%esi					;\
	orl	$IOAPIC_REDLO_MASK,%esi				;\
	andl	$~IOAPIC_REDLO_RIRR,%esi			;\
	movl	%esi,(%ebx)					;\
	movl	IS_PIC(%ebp),%edi				;\
	ioapic_asm_unlock(num)

/*
 * Since this is called just before the interrupt stub exits, AND
 * because the apic ACK doesn't use any registers, all registers
 * can be used here.
 * XXX this is not obvious
 */
#define ioapic_unmask(num) \
	movl    (%esp),%eax					;\
	cmpl    $IREENT_MAGIC,(TF_ERR+4)(%eax)			;\
	jne     79f						;\
	movl	IS_PIC(%ebp),%edi				;\
	ioapic_asm_lock(num)					;\
	movl	IS_PIN(%ebp),%esi				;\
	leal	0x10(%esi,%esi,1),%esi				;\
	movl	PIC_IOAPIC(%edi),%edi				;\
	movl	IOAPIC_SC_REG(%edi),%ebx			;\
	movl	IOAPIC_SC_DATA(%edi),%eax			;\
	movl	%esi, (%ebx)					;\
	movl	(%eax),%edx					;\
	andl	$~(IOAPIC_REDLO_MASK|IOAPIC_REDLO_RIRR),%edx	;\
	movl	%esi, (%ebx)					;\
	movl	%edx,(%eax)					;\
	movl	IS_PIC(%ebp),%edi				;\
	ioapic_asm_unlock(num)					;\
79:

#endif





/* $NetBSD: atomic.h,v 1.1 2002/10/19 12:22:34 bsh Exp $ */

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 2003-2004 Olivier Houchard
 * Copyright (C) 1994-1997 Mark Brinicombe
 * Copyright (C) 1994 Brini
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *  This product includes software developed by Brini.
 * 4. The name of Brini may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _WASM_ATOMIC_H_
#define _WASM_ATOMIC_H_

#include <sys/stdint.h>

#include <machine/wasm_inst.h>


#ifndef _KERNEL
#include <machine/sysarch.h>
#endif

// https://www.daemon-systems.org/man/atomic_add_32.3.html
// http://fxr.watson.org/fxr/source/arm/include/atomic.h?v=FREEBSD-6-0
// sys/kern/cddl/compat/opensolaris/kern/opensolaris_atomic.c
// start placeholders

void wasm32_atomic_fence(void);

// NetBSD atomics

__attribute__((always_inline))
static inline void wasm_atomic_add_32(volatile uint32_t *p, uint32_t val)
{
    __builtin_atomic_rmw_add32(p, val);
}

__attribute__((always_inline))
static inline void wasm_atomic_add_64(volatile uint64_t *p, uint64_t val)
{
    __builtin_atomic_rmw_add64(p, val);
}

__attribute__((always_inline))
static inline uint32_t wasm_atomic_add_32_nv(volatile uint32_t *p, uint32_t val)
{
    uint32_t ret = __builtin_atomic_rmw_add32(p, val);
    ret += val;
    return ret;
}

__attribute__((always_inline))
static inline uint64_t wasm_atomic_add_64_nv(volatile uint64_t *p, uint64_t val)
{
    uint64_t ret = __builtin_atomic_rmw_add64(p, val);
    ret += val;
    return ret;
}


__attribute__((always_inline))
static inline void wasm_atomic_and_32(volatile uint32_t *p, uint32_t val)
{
    __builtin_atomic_rmw_and32(p, val);
}


__attribute__((always_inline))
static inline void wasm_atomic_and_64(volatile uint64_t *p, uint64_t val)
{
    __builtin_atomic_rmw_add64(p, val);
}

__attribute__((always_inline))
static inline uint32_t wasm_atomic_and_32_nv(volatile uint32_t *p, uint32_t val)
{
    uint32_t ret = __builtin_atomic_rmw_and32(p, val);
    ret &= val;
    return ret;
}

__attribute__((always_inline))
static inline uint64_t wasm_atomic_and_64_nv(volatile uint64_t *p, uint64_t val)
{
    uint64_t ret = __builtin_atomic_rmw_add64(p, val);
    ret &= val;
    return ret;
}




__attribute__((always_inline))
static inline void wasm_atomic_or_32(volatile uint32_t *p, uint32_t val)
{
    __builtin_atomic_rmw_or32(p, val);
}

__attribute__((always_inline))
static inline void wasm_atomic_or_u32(volatile uint32_t *p, uint32_t val)
{
    __builtin_atomic_rmw_or32(p, val);
}


__attribute__((always_inline))
static inline void wasm_atomic_or_64(volatile uint64_t *p, uint64_t val)
{
    __builtin_atomic_rmw_or64(p, val);
}

__attribute__((always_inline))
static inline uint32_t wasm_atomic_or_32_nv(volatile uint32_t *p, uint32_t val)
{
    uint32_t ret = __builtin_atomic_rmw_or32(p, val);
    ret |= val;
    return ret;
}

__attribute__((always_inline))
static inline uint32_t wasm_atomic_or_u32_nv(volatile uint32_t *p, uint32_t val)
{
    uint32_t ret = __builtin_atomic_rmw_or32(p, val);
    ret |= val;
    return ret;
}

__attribute__((always_inline))
static inline uint64_t wasm_atomic_or_64_nv(volatile uint64_t *p, uint64_t val)
{
    uint64_t ret = __builtin_atomic_rmw_or64(p, val);
    ret |= val;
    return ret;
}

__attribute__((always_inline))
static inline uint32_t wasm_atomic_cas_32(volatile uint32_t *p, uint32_t expected, uint32_t replacement)
{
    return __builtin_atomic_rmw_cmpxchg32(p, expected, replacement);
}

__attribute__((always_inline))
static inline uint64_t wasm_atomic_cas_64(volatile uint64_t *p, uint64_t expected, uint64_t replacement)
{
    return __builtin_atomic_rmw_cmpxchg64(p, expected, replacement);
}


__attribute__((always_inline))
static inline uint32_t wasm_atomic_swap_32(volatile uint32_t *p, uint32_t val)
{
    return __builtin_atomic_rmw_xchg32(p, val);
}

__attribute__((always_inline))
static inline uint64_t wasm_atomic_swap_64(volatile uint64_t *p, uint64_t val)
{
    return __builtin_atomic_rmw_xchg64(p, val);
}



__attribute__((always_inline))
static inline void wasm_atomic_dec_32(volatile uint32_t *p)
{
    __builtin_atomic_rmw_sub32(p, 1);
}

__attribute__((always_inline))
static inline void wasm_atomic_dec_64(volatile uint64_t *p)
{
    __builtin_atomic_rmw_sub64(p, 1);
}

__attribute__((always_inline))
static inline uint32_t wasm_atomic_dec_32_nv(volatile uint32_t *p)
{
    uint32_t ret = __builtin_atomic_rmw_sub32(p, 1);
    ret = ret - 1;
    return ret;
}

__attribute__((always_inline))
static inline uint64_t wasm_atomic_dec_64_nv(volatile uint64_t *p)
{
    uint32_t ret = __builtin_atomic_rmw_sub64(p, 1);
    ret = ret - 1;
    return ret;
}


__attribute__((always_inline))
static inline void wasm_atomic_inc_32(volatile uint32_t *p)
{
    __builtin_atomic_rmw_add32(p, 1);
}

__attribute__((always_inline))
static inline void wasm_atomic_inc_64(volatile uint64_t *p)
{
    __builtin_atomic_rmw_add64(p, 1);
}

__attribute__((always_inline))
static inline uint32_t wasm_atomic_inc_32_nv(volatile uint32_t *p)
{
    uint32_t ret = __builtin_atomic_rmw_add32(p, 1);
    ret = ret + 1;
    return ret;
}

__attribute__((always_inline))
static inline uint64_t wasm_atomic_inc_64_nv(volatile uint64_t *p)
{
    uint32_t ret = __builtin_atomic_rmw_add64(p, 1);
    ret = ret + 1;
    return ret;
}

// all atomic handlers are prefixed with 'wasm_' in sys/sys/atomic.h

#define wasm_atomic_add_int         wasm_atomic_add_32
#define wasm_atomic_add_long        wasm_atomic_add_32
#define wasm_atomic_add_ptr         wasm_atomic_add_32
#define wasm_atomic_add_32_nv       wasm_atomic_add_32_nv
#define wasm_atomic_add_int_nv      wasm_atomic_add_32_nv
#define wasm_atomic_add_long_nv     wasm_atomic_add_32_nv
#define wasm_atomic_add_ptr_nv      wasm_atomic_add_32_nv
#define wasm_atomic_add_64_nv       wasm_atomic_add_64_nv
#define wasm_atomic_and_uint        wasm_atomic_and_32
#define wasm_atomic_and_ulong       wasm_atomic_and_32
#define wasm_atomic_and_64          wasm_atomic_and_64
#define wasm_atomic_and_32_nv       wasm_atomic_and_32_nv
#define wasm_atomic_and_uint_nv     wasm_atomic_and_32_nv
#define wasm_atomic_and_ulong_nv    wasm_atomic_and_32_nv
#define wasm_atomic_and_64_nv       wasm_atomic_and_64_nv
#define wasm_atomic_or_uint         wasm_atomic_or_32
#define wasm_atomic_or_ulong        wasm_atomic_or_32
#define wasm_atomic_or_64           wasm_atomic_or_64
#define wasm_atomic_or_32_nv        wasm_atomic_or_32_nv
#define wasm_atomic_or_uint_nv      wasm_atomic_or_32_nv
#define wasm_atomic_or_ulong_nv     wasm_atomic_or_32_nv
#define wasm_atomic_or_64_nv        wasm_atomic_or_64_nv
#define wasm_atomic_cas_uint        wasm_atomic_cas_32
#define wasm_atomic_cas_ulong       wasm_atomic_cas_32
#define wasm_atomic_cas_ptr         wasm_atomic_cas_32
#define wasm_atomic_cas_64          wasm_atomic_cas_64
#define wasm_atomic_cas_32_ni       wasm_atomic_cas_32
#define wasm_atomic_cas_uint_ni     wasm_atomic_cas_32
#define wasm_atomic_cas_ulong_ni    wasm_atomic_cas_32
#define wasm_atomic_cas_ptr_ni      wasm_atomic_cas_32
#define wasm_atomic_cas_64_ni       wasm_atomic_cas_64
#define wasm_atomic_swap_uint       wasm_atomic_swap_32
#define wasm_atomic_swap_ulong      wasm_atomic_swap_32
#define wasm_atomic_swap_ptr        wasm_atomic_swap_32
#define wasm_atomic_swap_64         wasm_atomic_swap_64
#define wasm_atomic_dec_uint        wasm_atomic_dec_32
#define wasm_atomic_dec_ulong       wasm_atomic_dec_32
#define wasm_atomic_dec_ptr         wasm_atomic_dec_32
#define wasm_atomic_dec_64          wasm_atomic_dec_64
#define wasm_atomic_dec_uint_nv		wasm_atomic_dec_32_nv
#define wasm_atomic_dec_ulong_nv	wasm_atomic_dec_32_nv
#define wasm_atomic_dec_ptr_nv      wasm_atomic_dec_32_nv
#define wasm_atomic_dec_64_nv       wasm_atomic_dec_64_nv
#define wasm_atomic_inc_uint        wasm_atomic_inc_32
#define wasm_atomic_inc_ulong       wasm_atomic_inc_32
#define wasm_atomic_inc_ptr         wasm_atomic_inc_32
#define wasm_atomic_inc_64          wasm_atomic_inc_64
#define wasm_atomic_inc_uint_nv     wasm_atomic_inc_32_nv
#define wasm_atomic_inc_ulong_nv    wasm_atomic_inc_32_nv
#define wasm_atomic_inc_ptr_nv      wasm_atomic_inc_32_nv
#define wasm_atomic_inc_64_nv       wasm_atomic_inc_64_nv

#define rmb wasm32_atomic_fence

#endif /* _MACHINE_ATOMIC_H_ */

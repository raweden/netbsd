/*	$NetBSD: wasm_locore.c,v 1.1 2023/05/07 12:41:49 skrll Exp $	*/

/*-
 * Copyright (c) 2020 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas of 3am Software Foundry, and by Nick Hudson.
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

//#include <sys/stdint.h>
//#include <uvm/pmap/pmap.h>


#include <sys/types.h>
#include <wasm/machdep.h>
#include <wasm/sbi.h>
#include <sys/null.h>
#include <sys/errno.h>

#include <wasm/wasm_module.h>

void __panic_abort(void) __WASM_IMPORT(kern, panic_abort);

#ifndef SBI_LEGACY_CONSOLE_PUTCHAR
#define	SBI_LEGACY_CONSOLE_PUTCHAR	1
#endif

#ifndef SBI_LEGACY_CONSOLE_GETCHAR
#define	SBI_LEGACY_CONSOLE_GETCHAR	2
#endif

// for two reasons these are set to another value than NULL.
// 1. to prevent the compiler to place them in .bss section which does 
//    not go in hand in hand with changing them in post-compile editing.
// 2. as address 0 (zero) might be a valid value in some cases.
#define WASM_UNINIT_ADDR (0xdeafbeef)

// these are not set by clang, but rather by wasm-info.. specifying default value to prevent them to end up in .bss
paddr_t __start__init_memory = WASM_UNINIT_ADDR; // start of the initial memory, often .rodata section.
paddr_t __stop__init_memory = WASM_UNINIT_ADDR;  // end of the initial memory, indicates the end of .bss section
paddr_t __bss_start = WASM_UNINIT_ADDR;          // start of the ".bss" section
paddr_t __kernel_text = WASM_UNINIT_ADDR;
paddr_t _end = WASM_UNINIT_ADDR;
paddr_t __data_start = WASM_UNINIT_ADDR;
paddr_t __rodata_start = WASM_UNINIT_ADDR;
paddr_t physical_start = WASM_UNINIT_ADDR;        // set at locore initialization at JavaScript side.
paddr_t physical_end = WASM_UNINIT_ADDR;
paddr_t bootstrap_pde = WASM_UNINIT_ADDR;
paddr_t l1_pte = WASM_UNINIT_ADDR;
paddr_t __fdt_base = WASM_UNINIT_ADDR;
unsigned long curphysmem = WASM_UNINIT_ADDR;

char **__wasmkern_envp;
char *lwp0uspace;

void main(void);


#ifdef __WASM
__attribute__((export_name("global_start")))
#endif

void global_start(void)
{
    if (__start__init_memory == WASM_UNINIT_ADDR ||
        __stop__init_memory == WASM_UNINIT_ADDR || 
        __bss_start == WASM_UNINIT_ADDR || 
        __kernel_text == WASM_UNINIT_ADDR || 
        _end == WASM_UNINIT_ADDR || 
        __data_start == WASM_UNINIT_ADDR || 
        __rodata_start == WASM_UNINIT_ADDR || 
        //physical_start == WASM_UNINIT_ADDR || 
        //physical_end == WASM_UNINIT_ADDR || 
        bootstrap_pde == WASM_UNINIT_ADDR || 
        l1_pte == WASM_UNINIT_ADDR) {
        // post-editing not applied..
        __panic_abort();
    }

    register_t hartid = 0;
    paddr_t dtb = __fdt_base;

    init_wasm32(hartid, dtb);
    main();
}

int wasmkern_getparam(const char *name, void *bufp, size_t *bufsz)
{
    if (bufp == NULL || bufsz == NULL) {
        return (EINVAL);
    }

    if (__wasmkern_envp == NULL) {
        return (ENOENT);
    }

    int nlen = strlen(name);
    
    for (int i=0; __wasmkern_envp[i]!=NULL; i++) {
        const char *entp = __wasmkern_envp[i];
        const char *eqp = strchr(entp, '=');
        int entnlen = eqp != NULL ? eqp - entp : -1;

        if (nlen != entnlen) {
            continue;
        }

        if (strncmp(entp, entp, nlen) == 0) {
            size_t strsz = strlen(eqp);
            size_t cpysz = MIN(strsz, *bufsz);
            printf("env: %s val: %s", name, eqp);
            memcpy(bufp, eqp, cpysz);
            *bufsz = strsz;

            return (0);
        }
    }

    return (ENOENT);
}

#undef WASM_UNINIT_ADDR

void uartputc(int chr)
{
    SBI_CALL1(SBI_LEGACY_CONSOLE_PUTCHAR, 0, chr);
}

int uartgetc(void)
{
    struct sbiret ret = SBI_CALL0(SBI_LEGACY_CONSOLE_PUTCHAR, 0);
    return ret.value;
}
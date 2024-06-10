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

#include <sys/stdint.h>

#ifdef __WASM
#define __WASM_BUILTIN(symbol) __attribute__((import_module("__builtin"), import_name(#symbol)))
#else
__WASM_BUILTIN(x)
#endif





/**
 * translates to `memory.fill` instruction in post-edit (or link-time with ylinker)
 *
 * @param dst The destination address.
 * @param val The value to use as fill
 * @param len The number of bytes to fill.
 */
void wasm_memory_fill(void * dst, int32_t val, uint32_t len) __WASM_BUILTIN(memory_fill);

/**
 * translates to `memory.copy` instruction in post-edit (or link-time with ylinker)
 *
 * @param dst The destination address.
 * @param src The value to use as fill
 * @param len The number of bytes to fill.
 */
void wasm_memory_copy(void * dst, const void *src, uint32_t len) __WASM_BUILTIN(memory_copy);

/**
 * translates to `memory.grow` instruction in post-edit or at link-time with ylinker.
 * @param pgcnt The number of wasm pages to grow memory with
 * @return The previous number of wasm pages or `-1` if memory could not be grown.
 */
int wasm_memory_grow(int pgcnt) __WASM_BUILTIN(memory_grow);
/**
 * translates to `memory.size` instruction in post-edit or at link-time with ylinker.
 * @return The number of wasm pages that the memory is currently grown to.
 */
int wasm_memory_size(void) __WASM_BUILTIN(memory_size);

int wasm_table_grow(int incr) __WASM_BUILTIN(table_grow);
int wasm_table_size(void) __WASM_BUILTIN(table_size);
void wasm_table_zerofill(uintptr_t dst, uintptr_t len) __WASM_BUILTIN(table_zerofill);
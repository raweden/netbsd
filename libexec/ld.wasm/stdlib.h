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

#ifndef __RTLD_WASM_STDLIB_H_
#define __RTLD_WASM_STDLIB_H_

#include <sys/errno.h>
#include <sys/stdint.h>
#include <sys/stdbool.h>

#include <dlfcn.h>

#include "rtsys.h"

// stdlib
extern int errno;
int strncmp(const char *s1, const char *s2, unsigned int n);
int strnlen(const char *s1, unsigned int n);
int strlen(const char *str);
char *strchr(const char *s, int c);
int strcmp(const char *s1, const char *s2);
char *strrchr(char const *s, int c);
void *memchr(const void *s, int c, size_t n);
size_t strlcpy(char *dst, const char *src, size_t size);
char *strncpy(char *dst, const char *src, size_t n);
char *strstr(const char *s, const char *find);
int sprintf(char *buf, int bufsz, const char *format, ...) __attribute__((__format__ (__printf__, 3, 4)));
int memcmp(const void *s1, const void *s2, size_t n);


// stdarg.h
typedef __builtin_va_list __va_list;
#ifndef __VA_LIST_DECLARED
typedef __va_list va_list;
#define __VA_LIST_DECLARED
#endif

#define	va_start(ap, last)	__builtin_va_start((ap), (last))
#define	va_arg			__builtin_va_arg
#define	va_end(ap)		__builtin_va_end(ap)
#define	__va_copy(dest, src)	__builtin_va_copy((dest), (src))

// printf.h
int vsnprintf(char *bf, size_t size, const char *fmt, va_list ap);

struct wasm_module_rt;

int _rtld_find_dso_library(const char *filepath, const char *pathbuf, uint32_t *pathsz);
int _rtld_load_dso_library(const char *filepath, struct wasm_module_rt **module);


#endif /* __RTLD_WASM_STDLIB_H_ */
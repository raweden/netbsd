/*	$NetBSD: wscanf.c,v 1.3 2013/04/19 23:32:17 joerg Exp $	*/

/*-
 * Copyright (c) 2002 Tim J. Robbins
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#if defined(LIBC_SCCS) && !defined(lint)
#if 0
__FBSDID("$FreeBSD: src/lib/libc/stdio/wscanf.c,v 1.1 2002/09/23 12:40:06 tjr Exp $");
#else
__RCSID("$NetBSD: wscanf.c,v 1.3 2013/04/19 23:32:17 joerg Exp $");
#endif
#endif /* LIBC_SCCS and not lint */

#include "namespace.h"

#include <stdarg.h>
#include <stdio.h>
#include <wchar.h>

#if defined(__weak_alias) && !defined(__WASM)
__weak_alias(wscanf_l, _wscanf_l)
#endif

int
wscanf(const wchar_t * __restrict fmt, ...)
{
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = vfwscanf(stdin, fmt, ap);
	va_end(ap);

	return r;
}

int
wscanf_l(locale_t loc, const wchar_t * __restrict fmt, ...)
{
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = vfwscanf_l(stdin, loc, fmt, ap);
	va_end(ap);

	return r;
}

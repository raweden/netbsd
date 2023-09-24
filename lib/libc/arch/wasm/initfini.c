/* 	$NetBSD: initfini.c,v 1.16 2023/07/18 11:44:32 riastradh Exp $	 */

/*-
 * Copyright (c) 2007 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Andrew Doran.
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
__RCSID("$NetBSD: initfini.c,v 1.16 2023/07/18 11:44:32 riastradh Exp $");

#ifdef _LIBC
#include "namespace.h"
#endif

#include <sys/param.h>
#include <sys/exec.h>
#include <stdbool.h>

static bool libc_initialised;

/*
 * Declare as common symbol to allow new libc with older binaries to
 * not trigger an undefined reference.
 */
struct ps_strings *__ps_strings;
char *__progname;
char **environ;

/*
 * _libc_init is called twice.  One call comes explicitly from crt0.o
 * (for newer versions) and the other is via global constructor handling.
 *
 * In static binaries the explicit call is first; in dynamically linked
 * binaries the global constructors of libc are called from ld.elf_so
 * before crt0.o is reached.
 *
 * Note that __ps_strings is set by crt0.o. So in the dynamic case, it
 * hasn't been set yet when we get here, and __libc_dlauxinfo is not
 * (ever) assigned. But this is ok because __libc_dlauxinfo is only
 * used in static binaries, because it's there to substitute for the
 * dynamic linker. In static binaries __ps_strings will have been set
 * up when we get here and we get a valid __libc_dlauxinfo.
 *
 * This code causes problems for Emacs because Emacs's undump
 * mechanism saves the __ps_strings value from the startup execution;
 * then running the resulting binary it gets here before crt0 has
 * assigned the current execution's value to __ps_strings, and in an
 * environment with ASLR this can cause the assignment of
 * __libc_dlauxinfo to receive SIGSEGV.
 */
void
_libc_init(void) __attribute__((__constructor__))
{

	if (libc_initialised)
		return;

	libc_initialised = 1;
}

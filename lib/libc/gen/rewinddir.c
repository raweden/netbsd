/*	$NetBSD: rewinddir.c,v 1.13 2010/09/26 02:26:59 yamt Exp $	*/

/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
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

#include <sys/cdefs.h>
#if defined(LIBC_SCCS) && !defined(lint)
#if 0
static char sccsid[] = "@(#)rewinddir.c	8.1 (Berkeley) 6/8/93";
#else
__RCSID("$NetBSD: rewinddir.c,v 1.13 2010/09/26 02:26:59 yamt Exp $");
#endif
#endif /* LIBC_SCCS and not lint */

#include "namespace.h"
#include "reentrant.h"
#include "extern.h"
#include <sys/types.h>

#include <unistd.h>
#include <dirent.h>

#include "dirent_private.h"

#if defined(__weak_alias) && !defined(__WASM)
__weak_alias(rewinddir,_rewinddir)
#endif

void
rewinddir(DIR *dirp)
{
	int fd;

#ifdef _REENTRANT
	if (__isthreaded) {
		mutex_lock((mutex_t *)dirp->dd_lock);
	}
#endif
	fd = dirp->dd_fd;
	_finidir(dirp);
	dirp->dd_seek = lseek(fd, (off_t)0, SEEK_SET);
	_initdir(dirp, fd, NULL);
#ifdef _REENTRANT
	if (__isthreaded) {
		mutex_unlock((mutex_t *)dirp->dd_lock);
	}
#endif
}

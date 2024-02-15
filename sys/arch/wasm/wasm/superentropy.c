/*	$NetBSD: superentropy.c,v 0.1 2023-12-11 19:23:51 raweden Exp $	*/

/*
 * Copyright (c) 2023 Jesper Svensson.  All Rights Reserved.
 * based on source of rumpkern which was written by Antti Kantee.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "errno.h"
#include <stddef.h>
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: superentropy.c,v 0.1 2023-12-11 19:23:51 raweden Exp $");

#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/rndsource.h>

#include <wasm/wasm_module.h>

int __random_source(void *, int, int*) __WASM_IMPORT(kern, random_source);

static krndsource_t rndsrc;

static void
feedrandom(size_t bytes, void *cookie __unused)
{
	uint8_t *rnddata;
	int n, nread;

	rnddata = kmem_alloc(bytes, KM_SLEEP);
	n = 0;

	while (n < bytes) {
        if (__random_source(rnddata + n, bytes - n, &nread) != 0)
            break;
		n += MIN(nread, bytes - n);
	}

	if (n) {
		rnd_add_data_sync(&rndsrc, rnddata, n, NBBY*n);
	}

	kmem_free(rnddata, bytes);
}

void
wasm_superentropy_init(void)
{
	rndsource_setcb(&rndsrc, &feedrandom, NULL);
	rnd_attach_source(&rndsrc, "wasm_entropy", RND_TYPE_VM, RND_FLAG_COLLECT_VALUE | RND_FLAG_HASCB);
}

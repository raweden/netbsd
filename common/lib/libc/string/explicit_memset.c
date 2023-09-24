/* $NetBSD: explicit_memset.c,v 1.4 2014/06/24 16:39:39 drochner Exp $ */

/*
 * Written by Matthias Drochner <drochner@NetBSD.org>.
 * Public domain.
 */

#if !defined(_KERNEL) && !defined(_STANDALONE)
#include "namespace.h"
#include <string.h>
void *_explicit_memset(void *, int, size_t) __attribute__((weak, alias("explicit_memset")));
#define explicit_memset_impl __explicit_memset_impl
#else
#include <lib/libkern/libkern.h>
#endif

/*
 * The use of a volatile pointer guarantees that the compiler
 * will not optimise the call away.
 */
void *(* volatile explicit_memset_impl)(void *, int, size_t) = memset;

void *
explicit_memset(void *b, int c, size_t len)
{

	return (*explicit_memset_impl)(b, c, len);
}

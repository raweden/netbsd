/* $NetBSD: consttime_memequal.c,v 1.6 2015/03/18 20:11:35 riastradh Exp $ */

/*
 * Written by Matthias Drochner <drochner@NetBSD.org>.
 * Public domain.
 */

#if !defined(_KERNEL) && !defined(_STANDALONE)
#include "namespace.h"
#include <string.h>
int consttime_memequal(const void *, const void *, size_t);
int _consttime_memequal(const void *, const void *, size_t) __attribute__((weak, alias("consttime_memequal")));
#else
#include <lib/libkern/libkern.h>
#endif

int
consttime_memequal(const void *b1, const void *b2, size_t len)
{
	const unsigned char *c1 = b1, *c2 = b2;
	unsigned int res = 0;

	while (len--)
		res |= *c1++ ^ *c2++;

	/*
	 * Map 0 to 1 and [1, 256) to 0 using only constant-time
	 * arithmetic.
	 *
	 * This is not simply `!res' because although many CPUs support
	 * branchless conditional moves and many compilers will take
	 * advantage of them, certain compilers generate branches on
	 * certain CPUs for `!res'.
	 */
	return (1 & ((res - 1) >> 8));
}

/*	$NetBSD: murmurhash.c,v 1.8 2019/08/20 15:17:02 para Exp $	*/

/*
 * MurmurHash2 -- from the original code:
 *
 * "MurmurHash2 was written by Austin Appleby, and is placed in the public
 * domain. The author hereby disclaims copyright to this source code."
 *
 * References:
 *	http://code.google.com/p/smhasher/
 *	https://sites.google.com/site/murmurhash/
 */

#include <sys/cdefs.h>

#if defined(_KERNEL) || defined(_STANDALONE)
__KERNEL_RCSID(0, "$NetBSD: murmurhash.c,v 1.8 2019/08/20 15:17:02 para Exp $");

#include <lib/libkern/libkern.h>

#else

#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: murmurhash.c,v 1.8 2019/08/20 15:17:02 para Exp $");
#endif /* LIBC_SCCS and not lint */

#include "namespace.h"
#include <string.h>

#endif

#include <sys/types.h>
#include <sys/param.h>
#include <sys/hash.h>

#if !defined(_KERNEL) && !defined(_STANDALONE)
uint32_t _murmurhash2(const void *, size_t, uint32_t) __attribute__((weak, alias("murmurhash2")));
#endif

uint32_t
murmurhash2(const void *key, size_t len, uint32_t seed)
{
	/*
	 * Note: 'm' and 'r' are mixing constants generated offline.
	 * They're not really 'magic', they just happen to work well.
	 * Initialize the hash to a 'random' value.
	 */
	const uint32_t m = 0x5bd1e995;
	const int r = 24;

	const uint8_t *data = key;
	uint32_t h = seed ^ (uint32_t)len;

	if (__predict_true(ALIGNED_POINTER(key, uint32_t))) {
		while (len >= sizeof(uint32_t)) {
			uint32_t k;

			ALIGNED_POINTER_LOAD(&k, data, uint32_t);
			k = htole32(k);

			k *= m;
			k ^= k >> r;
			k *= m;

			h *= m;
			h ^= k;

			data += sizeof(uint32_t);
			len -= sizeof(uint32_t);
		}
	} else {
		while (len >= sizeof(uint32_t)) {
			uint32_t k;

			k  = data[0];
			k |= data[1] << 8;
			k |= data[2] << 16;
			k |= data[3] << 24;

			k *= m;
			k ^= k >> r;
			k *= m;

			h *= m;
			h ^= k;

			data += sizeof(uint32_t);
			len -= sizeof(uint32_t);
		}
	}

	/* Handle the last few bytes of the input array. */
	switch (len) {
	case 3:
		h ^= data[2] << 16;
		/* FALLTHROUGH */
	case 2:
		h ^= data[1] << 8;
		/* FALLTHROUGH */
	case 1:
		h ^= data[0];
		h *= m;
	}

	/*
	 * Do a few final mixes of the hash to ensure the last few
	 * bytes are well-incorporated.
	 */
	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
}

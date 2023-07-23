/*      $NetBSD: bswap.h,v 1.1 2014/09/19 17:36:26 matt Exp $      */

#ifndef _WASM_BSWAP_H_
#define _WASM_BSWAP_H_

#include <machine/byte_swap.h>

#define __BSWAP_RENAME
#include <sys/bswap.h>

#endif /* _WASM_BSWAP_H_ */

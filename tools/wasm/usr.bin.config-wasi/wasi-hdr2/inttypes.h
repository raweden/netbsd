
#ifndef __WASI_NB_INTTYPES_H_
#define __WASI_NB_INTTYPES_H_

#include "/home/raweden/wasi-sdk-20.0/share/wasi-sysroot/include/inttypes.h"



intmax_t	strtoi(const char * __restrict, char ** __restrict, int,
	               intmax_t, intmax_t, int *);
uintmax_t	strtou(const char * __restrict, char ** __restrict, int,
	               uintmax_t, uintmax_t, int *);

#endif
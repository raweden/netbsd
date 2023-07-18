
#ifndef __WASI_NB_STDLIB_H_
#define __WASI_NB_STDLIB_H_

#include "/home/raweden/wasi-sdk-20.0/share/wasi-sysroot/include/stdlib.h"

char *realpath(const char * __restrict, char * __restrict);

int	 mkstemp(char *);

int	reallocarr(void *, size_t, size_t);

#endif
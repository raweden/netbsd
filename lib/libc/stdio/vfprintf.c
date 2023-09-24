#define NARROW
#include "vfwprintf.c"

#if defined(__weak_alias) && !defined(__WASM)
__weak_alias(vfprintf_l, _vfprintf_l)
#endif

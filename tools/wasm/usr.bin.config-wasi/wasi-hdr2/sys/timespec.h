
#ifndef __WASI_SYS_TIMESPEC_H_
#define __WASI_SYS_TIMESPEC_H_

// a hack to prevent netbsd to declare suseconds_t as long long
#ifdef suseconds_t
#undef suseconds_t
#endif

#include "/home/raweden/wasi-sdk-20.0/share/wasi-sysroot/include/__struct_timespec.h"

#if 0

#define timeval __unused_timeval
#include "../../sys/sys/timespec.h"
#undef timeval

#endif

#endif
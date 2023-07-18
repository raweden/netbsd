
#ifndef __WASI_SYS_TIME_H_
#define __WASI_SYS_TIME_H_

// a hack to prevent netbsd to declare suseconds_t as long long
#ifdef suseconds_t
#undef suseconds_t
#endif

#include "/home/raweden/wasi-sdk-20.0/share/wasi-sysroot/include/sys/time.h"
#include "/home/raweden/wasi-sdk-20.0/share/wasi-sysroot/include/__struct_timespec.h"
#include "/home/raweden/wasi-sdk-20.0/share/wasi-sysroot/include/__struct_timeval.h"

#if 0

#define timeval __unused_timeval
#include "../../sys/sys/time.h"
#undef timeval

#endif

#endif
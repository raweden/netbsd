

#include "cdefs.h"

// check lib/libc/softfloat/softfloat-for-gcc.h
//       lib/libc/arch/aarch64/softfloat/softfloat.h

// float128_mul
long double __noinline
__multf3(long double a, long double b)
{
    return a * b;
}

// int64_to_float128
long double __noinline
__floatditf(long long b)
{
    return 0;
}

// float128_add
long double __noinline
__addtf3(long double a, long double b)
{
    return a + b;
}

// float128_to_float32
float __noinline
__trunctfsf2(long long a, long long b)
{
    return (float)b;
}

// float128_to_float64
double __noinline
__trunctfdf2(long long a, long long b)
{
    return (double)b;
}
/* @(#)w_exp.c 5.1 93/09/24 */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */

#include <sys/cdefs.h>
#if defined(LIBM_SCCS) && !defined(lint)
__RCSID("$NetBSD: w_exp.c,v 1.10 2007/08/20 16:01:40 drochner Exp $");
#endif

/*
 * wrapper exp(x)
 */

#include "namespace.h"
#include "math.h"
#include "math_private.h"

#if defined(__weak_alias) && !defined (__WASM)
__weak_alias(exp, _exp)
#endif

static const double
o_threshold=  7.09782712893383973096e+02,  /* 0x40862E42, 0xFEFA39EF */
u_threshold= -7.45133219101941108420e+02;  /* 0xc0874910, 0xD52D3051 */

double
exp(double x)		/* wrapper exp */
{
#ifdef _IEEE_LIBM
	return __ieee754_exp(x);
#else
	double z;
	z = __ieee754_exp(x);
	if(_LIB_VERSION == _IEEE_) return z;
	if(finite(x)) {
	    if(x>o_threshold)
	        return __kernel_standard(x,x,6); /* exp overflow */
	    else if(x<u_threshold)
	        return __kernel_standard(x,x,7); /* exp underflow */
	}
	return z;
#endif
}

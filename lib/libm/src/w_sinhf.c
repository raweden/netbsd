/* w_sinhf.c -- float version of w_sinh.c.
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 */

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
__RCSID("$NetBSD: w_sinhf.c,v 1.7 2007/08/20 16:01:40 drochner Exp $");
#endif

/*
 * wrapper sinhf(x)
 */

#include "namespace.h"
#include "math.h"
#include "math_private.h"

#if defined(__weak_alias) && !defined (__WASM)
__weak_alias(sinhf, _sinhf)
#endif

float
sinhf(float x)		/* wrapper sinhf */
{
#ifdef _IEEE_LIBM
	return __ieee754_sinhf(x);
#else
	float z;
	z = __ieee754_sinhf(x);
	if(_LIB_VERSION == _IEEE_) return z;
	if(!finitef(z)&&finitef(x)) {
	    /* sinhf overflow */
	    return (float)__kernel_standard((double)x,(double)x,125);
	} else
	    return z;
#endif
}

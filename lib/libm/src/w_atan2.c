/* @(#)w_atan2.c 5.1 93/09/24 */
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
__RCSID("$NetBSD: w_atan2.c,v 1.10 2007/08/10 21:20:36 drochner Exp $");
#endif

/*
 * wrapper atan2(y,x)
 */

#include "namespace.h"
#include "math.h"
#include "math_private.h"

#if defined(__weak_alias) && !defined (__WASM)
__weak_alias(atan2, _atan2)
#endif

double
atan2(double y, double x)	/* wrapper atan2 */
{
#ifdef _IEEE_LIBM
	return __ieee754_atan2(y,x);
#else
	double z;
	z = __ieee754_atan2(y,x);
	if(_LIB_VERSION == _IEEE_||isnan(x)||isnan(y)) return z;
	if(x==0.0&&y==0.0) {
	        return __kernel_standard(y,x,3); /* atan2(+-0,+-0) */
	} else
	    return z;
#endif
}

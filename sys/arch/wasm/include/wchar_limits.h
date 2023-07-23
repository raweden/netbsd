#ifndef _WASM_WCHAR_LIMITS_H_
#define _WASM_WCHAR_LIMITS_H_

/*
 * 7.18.3 Limits of other integer types
 */

/* limits of wchar_t */

#ifdef __WCHAR_MIN__
#define	WCHAR_MIN	__WCHAR_MIN__			/* wchar_t	  */
#elif __WCHAR_UNSIGNED__
#define	WCHAR_MIN	0U				/* wchar_t	  */
#else
#define	WCHAR_MIN	(-0x7fffffff-1)			/* wchar_t	  */
#endif

#ifdef __WCHAR_MAX__
#define	WCHAR_MAX	__WCHAR_MAX__			/* wchar_t	  */
#elif __WCHAR_UNSIGNED__
#define	WCHAR_MAX	0xffffffffU			/* wchar_t	  */
#else
#define	WCHAR_MAX	0x7fffffff			/* wchar_t	  */
#endif

/* limits of wint_t */

#ifdef __WINT_MIN__
#define	WINT_MIN	__WINT_MIN__			/* wint_t	  */
#elif __WINT_UNSIGNED__
#define	WINT_MIN	0U				/* wint_t	  */
#else
#define	WINT_MIN	(-0x7fffffff-1)			/* wint_t	  */
#endif

#ifdef __WINT_MAX__
#define	WINT_MAX	__WINT_MAX__			/* wint_t	  */
#elif __WINT_UNSIGNED__
#define	WINT_MAX	0xffffffffU			/* wint_t	  */
#else
#define	WINT_MAX	0x7fffffff			/* wint_t	  */
#endif

#endif /* !_WASM_WCHAR_LIMITS_H_ */
/* $NetBSD: cdefs.h,v 1.2 2023/05/07 12:41:48 skrll Exp $ */

#ifndef _WASM_CDEFS_H_
#define _WASM_CDEFS_H_

#define	__ALIGNBYTES	((size_t)(__BIGGEST_ALIGNMENT__ - 1U))


#ifdef _KERNEL

/*
 * On multiprocessor systems we can gain an improvement in performance
 * by being mindful of which cachelines data is placed in.
 *
 * __read_mostly:
 *
 *	It makes sense to ensure that rarely modified data is not
 *	placed in the same cacheline as frequently modified data.
 *	To mitigate the phenomenon known as "false-sharing" we
 *	can annotate rarely modified variables with __read_mostly.
 *	All such variables are placed into the .data.read_mostly
 *	section in the kernel ELF.
 *
 *	Prime candidates for __read_mostly annotation are variables
 *	which are hardly ever modified and which are used in code
 *	hot-paths, e.g. pmap_initialized.
 *
 * __cacheline_aligned:
 *
 *	Some data structures (mainly locks) benefit from being aligned
 *	on a cacheline boundary, and having a cacheline to themselves.
 *	This way, the modification of other data items cannot adversely
 *	affect the lock and vice versa.
 *
 *	Any variables annotated with __cacheline_aligned will be
 *	placed into the .data.cacheline_aligned ELF section.
 */
#define	__read_mostly						\
    __attribute__((__section__(".data.read_mostly")))

#define	__cacheline_aligned					\
    __attribute__((__aligned__(COHERENCY_UNIT),			\
		 __section__(".data.cacheline_aligned")))



#ifdef __WASM
#undef __read_mostly
#undef __read_frequently
#undef __exclusive_cache_line
#undef __builtin_return_address
#define __read_mostly
#define __read_frequently
#define __exclusive_cache_line
#define __builtin_return_address(x) (0)
#endif

#endif /* _KERNEL */

#endif /* _WASM_CDEFS_H_ */

/* $NetBSD: fdt_dma_machdep.c,v 1.1 2020/07/16 11:49:38 jmcneill Exp $ */

/*-
 * Copyright (c) 2020 Jared McNeill <jmcneill@invisible.ca>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: fdt_dma_machdep.c,v 1.1 2020/07/16 11:49:38 jmcneill Exp $");

#define	_MIPS_BUS_DMA_PRIVATE

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kmem.h>

#include <dev/fdt/fdtvar.h>

#include <machine/bus_defs.h>
#include <machine/bus_funcs.h>

#if 0
/*
 * DMA mapping methods.
 */
struct wasm_bus_dmamap_ops {
	int	 (*dmamap_create)(bus_dma_tag_t, bus_size_t, int, bus_size_t, bus_size_t, int, bus_dmamap_t *);
	void (*dmamap_destroy)(bus_dma_tag_t, bus_dmamap_t);
	int	 (*dmamap_load)(bus_dma_tag_t, bus_dmamap_t, void *, bus_size_t, struct proc *, int);
	int	 (*dmamap_load_mbuf)(bus_dma_tag_t, bus_dmamap_t, struct mbuf *, int);
	int	 (*dmamap_load_uio)(bus_dma_tag_t, bus_dmamap_t, struct uio *, int);
	int	 (*dmamap_load_raw)(bus_dma_tag_t, bus_dmamap_t, bus_dma_segment_t *, int, bus_size_t, int);
	void (*dmamap_unload)(bus_dma_tag_t, bus_dmamap_t);
	void (*dmamap_sync)(bus_dma_tag_t, bus_dmamap_t, bus_addr_t, bus_size_t, int);
};

struct wasm_bus_dmamem_ops {
	int	    (*dmamem_alloc)(bus_dma_tag_t, bus_size_t, bus_size_t, bus_size_t, bus_dma_segment_t *, int, int *, int);
	void	(*dmamem_free)(bus_dma_tag_t, bus_dma_segment_t *, int);
	int	    (*dmamem_map)(bus_dma_tag_t, bus_dma_segment_t *, int, size_t, void **, int);
	void	(*dmamem_unmap)(bus_dma_tag_t, void *, size_t);
	paddr_t	(*dmamem_mmap)(bus_dma_tag_t, bus_dma_segment_t *, int, off_t, int, int);
};


struct wasm_bus_dmatag_ops {
	int	 (*dmatag_subregion)(bus_dma_tag_t, bus_addr_t, bus_addr_t, bus_dma_tag_t *, int);
	void (*dmatag_destroy)(bus_dma_tag_t);
};

int	 _bus_dmamap_create(bus_dma_tag_t, bus_size_t, int, bus_size_t, bus_size_t, int, bus_dmamap_t *);
void _bus_dmamap_destroy(bus_dma_tag_t, bus_dmamap_t);
int	 _bus_dmamap_load(bus_dma_tag_t, bus_dmamap_t, void *, bus_size_t, struct proc *, int);
int	 _bus_dmamap_load_mbuf(bus_dma_tag_t, bus_dmamap_t, struct mbuf *, int);
int	 _bus_dmamap_load_uio(bus_dma_tag_t, bus_dmamap_t, struct uio *, int);
int	 _bus_dmamap_load_raw(bus_dma_tag_t, bus_dmamap_t, bus_dma_segment_t *, int, bus_size_t, int);
void _bus_dmamap_unload(bus_dma_tag_t, bus_dmamap_t);
void _bus_dmamap_sync(bus_dma_tag_t, bus_dmamap_t, bus_addr_t, bus_size_t, int);

int	    _bus_dmamem_alloc(bus_dma_tag_t, bus_size_t, bus_size_t, bus_size_t, bus_dma_segment_t *, int, int *, int);
void	_bus_dmamem_free(bus_dma_tag_t, bus_dma_segment_t *, int);
int	    _bus_dmamem_map(bus_dma_tag_t, bus_dma_segment_t *, int, size_t, void **, int);
void	_bus_dmamem_unmap(bus_dma_tag_t, void *, size_t);
paddr_t	_bus_dmamem_mmap(bus_dma_tag_t, bus_dma_segment_t *, int, off_t, int, int);

int	 _bus_dmatag_subregion(bus_dma_tag_t, bus_addr_t, bus_addr_t, bus_dma_tag_t *, int);
void _bus_dmatag_destroy(bus_dma_tag_t);

/*
 *	bus_dma_tag_t
 *
 *	A machine-dependent opaque type describing the implementation of
 *	DMA for a given bus.
 */
struct wasm_bus_dma_tag {
	void	*_cookie;		/* cookie used in the guts */

	bus_addr_t _wbase;		/* DMA window base */
	int _tag_needs_free;		/* number of references (maybe 0) */
	bus_addr_t _bounce_thresh;
	bus_addr_t _bounce_alloc_lo;	/* physical base of the window */
	bus_addr_t _bounce_alloc_hi;	/* physical limit of the windows */
	int	(*_may_bounce)(bus_dma_tag_t, bus_dmamap_t, int, int *);

	struct wasm_bus_dmamap_ops _dmamap_ops;
	struct wasm_bus_dmamem_ops _dmamem_ops;
	struct wasm_bus_dmatag_ops _dmatag_ops;
};

#define	_BUS_DMAMAP_OPS_INITIALIZER {					\
		.dmamap_create		= _bus_dmamap_create,		\
		.dmamap_destroy		= _bus_dmamap_destroy,		\
		.dmamap_load		= _bus_dmamap_load,		    \
		.dmamap_load_mbuf	= _bus_dmamap_load_mbuf,	\
		.dmamap_load_uio	= _bus_dmamap_load_uio,		\
		.dmamap_load_raw	= _bus_dmamap_load_raw,		\
		.dmamap_unload		= _bus_dmamap_unload,		\
		.dmamap_sync		= _bus_dmamap_sync,		    \
	}

#define	_BUS_DMAMEM_OPS_INITIALIZER {					\
		.dmamem_alloc = 	_bus_dmamem_alloc,		    \
		.dmamem_free =		_bus_dmamem_free,		    \
		.dmamem_map =		_bus_dmamem_map,		    \
		.dmamem_unmap =		_bus_dmamem_unmap,		    \
		.dmamem_mmap =		_bus_dmamem_mmap,		    \
	}

#define	_BUS_DMATAG_OPS_INITIALIZER {					\
		.dmatag_subregion =	_bus_dmatag_subregion,		\
		.dmatag_destroy =	_bus_dmatag_destroy,		\
	}

#endif


extern struct wasm_bus_dma_tag wasm_generic_dma_tag;

bus_dma_tag_t
fdtbus_dma_tag_create(int phandle, const struct fdt_dma_range *ranges, u_int nranges)
{
	struct wasm_bus_dma_tag *tagp;
	u_int n;

	// Check bindings.
	const int flags = of_hasprop(phandle, "dma-coherent") ?
	    _BUS_DMAMAP_COHERENT : 0;

	tagp = kmem_alloc(sizeof(*tagp), KM_SLEEP);
	*tagp = wasm_generic_dma_tag;
	if (nranges == 0) {
		tagp->_nranges = 1;
		tagp->_ranges = kmem_alloc(sizeof(*tagp->_ranges), KM_SLEEP);
		tagp->_ranges[0].dr_sysbase = 0;
		tagp->_ranges[0].dr_busbase = 0;
		tagp->_ranges[0].dr_len = UINTPTR_MAX;
		tagp->_ranges[0].dr_flags = flags;
	} else {
		tagp->_nranges = nranges;
		tagp->_ranges = kmem_alloc(sizeof(*tagp->_ranges) * nranges,
		    KM_SLEEP);
		for (n = 0; n < nranges; n++) {
			tagp->_ranges[n].dr_sysbase = ranges[n].dr_sysbase;
			tagp->_ranges[n].dr_busbase = ranges[n].dr_busbase;
			tagp->_ranges[n].dr_len = ranges[n].dr_len;
			tagp->_ranges[n].dr_flags = flags;
		}
	}

	return tagp;
}



#ifndef _WASM_PLICVAR_H
#define	_WASM_PLICVAR_H

struct plic_intrhand {
	int	(*ih_func)(void *);
	void	*ih_arg;
	bool	ih_mpsafe;
	u_int	ih_irq;
	u_int	ih_cidx;
};

struct plic_softc {
	device_t		sc_dev;
	bus_space_tag_t		sc_bst;
	bus_space_handle_t	sc_bsh;

	uint32_t		sc_ndev;

	uint32_t		sc_context[MAXCPUS];
	struct plic_intrhand	*sc_intr;
	struct evcnt		*sc_intrevs;
};

#endif
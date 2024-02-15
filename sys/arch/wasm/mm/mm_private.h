

#ifndef _WASM_MM_PRIVATE_H_
#define _WASM_MM_PRIVATE_H_

struct mm_page;

struct uvmpdpol_globalstate {
	kmutex_t lock;			/* lock on state */
					/* <= compiler pads here */
	struct mm_page *s_activeq;		/* allocated pages, in use */
	struct mm_page *s_inactiveq;	/* pages between the clock hands */
	int s_active;
	int s_inactive;
	int s_inactarg;
	struct uvm_pctparam s_anonmin;
	struct uvm_pctparam s_filemin;
	struct uvm_pctparam s_execmin;
	struct uvm_pctparam s_anonmax;
	struct uvm_pctparam s_filemax;
	struct uvm_pctparam s_execmax;
	struct uvm_pctparam s_inactivepct;
};

#endif /* _WASM_MM_PRIVATE_H_ */
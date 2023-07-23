#include "opt_console.h"

#include <sys/cdefs.h>
__KERNEL_RCSID(1, "$NetBSD: bus_space_generic.c,v 1.2 2023/07/02 12:30:48 skrll Exp $");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <wasm/wasm_module.h>
void __panic_abort(void) __WASM_IMPORT(kern, panic_abort);

/* read (single) */
uint8_t generic_bs_r_1(void *tag, bus_space_handle_t addr, bus_size_t offset)
{
    __panic_abort();
    return 0;
}

uint16_t generic_bs_r_2(void *tag, bus_space_handle_t addr, bus_size_t offset)
{
    __panic_abort();
    return 0;
}

uint32_t generic_bs_r_4(void *tag, bus_space_handle_t addr, bus_size_t offset)
{
    __panic_abort();
    return 0;
}

uint64_t bs_notimpl_bs_r_8(void *tag, bus_space_handle_t addr, bus_size_t offset)
{
    __panic_abort();
    return 0;
}

/* read multiple */
void generic_bs_rm_1(void *tag, bus_space_handle_t addr, bus_size_t offset, uint8_t *datap, bus_size_t count)
{
    __panic_abort();
}

void generic_bs_rm_2(void *tag, bus_space_handle_t addr, bus_size_t offset, uint16_t *datap, bus_size_t count)
{
    __panic_abort();
}

void generic_bs_rm_4(void *tag, bus_space_handle_t addr, bus_size_t offset, uint32_t *datap, bus_size_t count)
{
    __panic_abort();
}

void bs_notimpl_bs_rm_8(void *tag, bus_space_handle_t addr, bus_size_t offset, uint64_t *datap, bus_size_t count)
{
    __panic_abort();
}

/* read region */
void generic_bs_rr_1(void *tag, bus_space_handle_t addr, bus_size_t offset, uint8_t *datap, bus_size_t count)
{
    __panic_abort();
}

void bs_notimpl_bs_rr_2(void *tag, bus_space_handle_t addr, bus_size_t offset, uint16_t *datap, bus_size_t count)
{
    __panic_abort();
}

void generic_bs_rr_2(void *tag, bus_space_handle_t addr, bus_size_t offset, uint16_t *datap, bus_size_t count)
{
    __panic_abort();
}



void bs_notimpl_bs_rr_4(void *tag, bus_space_handle_t addr, bus_size_t offset, uint32_t *datap, bus_size_t count)
{
    __panic_abort();
}

void bs_notimpl_bs_rr_8(void *tag, bus_space_handle_t addr, bus_size_t offset, uint64_t *datap, bus_size_t count)
{
    __panic_abort();
}

/* void bs_rr_4(a0: tag, a1: addr, a2: offset, a3: datap, a4: count); */

/* write (single) */
void generic_bs_w_1(void *tag, bus_space_handle_t addr, bus_size_t offset, uint8_t value)
{
    __panic_abort();
}

void generic_bs_w_2(void *tag, bus_space_handle_t addr, bus_size_t offset, uint16_t value)
{
    __panic_abort();
}

void generic_bs_w_4(void *tag, bus_space_handle_t addr, bus_size_t offset, uint32_t value)
{
    __panic_abort();
}

void bs_notimpl_bs_w_8(void *tag, bus_space_handle_t addr, bus_size_t offset, uint64_t value)
{
    __panic_abort();
}



/* write multiple */
void generic_bs_wm_1(void *tag, bus_space_handle_t addr, bus_size_t offset, const uint8_t *datap, bus_size_t count)
{
    __panic_abort();
}

void generic_bs_wm_2(void *tag, bus_space_handle_t addr, bus_size_t offset, const uint16_t *datap, bus_size_t count)
{
    __panic_abort();
}

void generic_bs_wm_4(void *tag, bus_space_handle_t addr, bus_size_t offset, const uint32_t *datap, bus_size_t count)
{
    __panic_abort();
}

void bs_notimpl_bs_wm_8(void *tag, bus_space_handle_t addr, bus_size_t offset, const uint64_t *datap, bus_size_t count)
{
    __panic_abort();
}


/* write region */
void bs_notimpl_bs_wr_1(void *tag, bus_space_handle_t addr, bus_size_t offset, const uint8_t *datap, bus_size_t count)
{
    __panic_abort();
}

void bs_notimpl_bs_wr_2(void *tag, bus_space_handle_t addr, bus_size_t offset, const uint16_t *datap, bus_size_t count)
{
    __panic_abort();
}

void bs_notimpl_bs_wr_4(void *tag, bus_space_handle_t addr, bus_size_t offset, const uint32_t *datap, bus_size_t count)
{
    __panic_abort();
}

void bs_notimpl_bs_wr_8(void *tag, bus_space_handle_t addr, bus_size_t offset, const uint64_t *datap, bus_size_t count)
{
    __panic_abort();
}


/* set multiple */
void bs_notimpl_bs_sm_1(void *tag, bus_space_handle_t addr, bus_size_t offset, uint8_t value, bus_size_t count)
{
    __panic_abort();
}

void bs_notimpl_bs_sm_2(void *tag, bus_space_handle_t addr, bus_size_t offset, uint16_t value, bus_size_t count)
{
    __panic_abort();
}

void bs_notimpl_bs_sm_4(void *tag, bus_space_handle_t addr, bus_size_t offset, uint32_t value, bus_size_t count)
{
    __panic_abort();
}

void bs_notimpl_bs_sm_8(void *tag, bus_space_handle_t addr, bus_size_t offset, uint64_t value, bus_size_t count)
{
    __panic_abort();
}


/* set region */
void bs_notimpl_bs_sr_1(void *tag, bus_space_handle_t addr, bus_size_t offset, uint8_t value, bus_size_t count)
{
    __panic_abort();
}

void bs_notimpl_bs_sr_2(void *tag, bus_space_handle_t addr, bus_size_t offset, uint16_t value, bus_size_t count)
{
    __panic_abort();
}

void bs_notimpl_bs_sr_4(void *tag, bus_space_handle_t addr, bus_size_t offset, uint32_t value, bus_size_t count)
{
    __panic_abort();
}

void bs_notimpl_bs_sr_8(void *tag, bus_space_handle_t addr, bus_size_t offset, uint64_t value, bus_size_t count)
{
    __panic_abort();
}

/* copy */
void bs_notimpl_bs_c_1(void *tag, bus_space_handle_t src, bus_size_t srcoff, bus_space_handle_t dst, bus_size_t dstoff, bus_size_t count)
{
    __panic_abort();
}

void bs_notimpl_bs_c_2(void *tag, bus_space_handle_t src, bus_size_t srcoff, bus_space_handle_t dst, bus_size_t dstoff, bus_size_t count)
{
    __panic_abort();
}

void bs_notimpl_bs_c_4(void *tag, bus_space_handle_t src, bus_size_t srcoff, bus_space_handle_t dst, bus_size_t dstoff, bus_size_t count)
{
    __panic_abort();
}

void bs_notimpl_bs_c_8(void *tag, bus_space_handle_t src, bus_size_t srcoff, bus_space_handle_t dst, bus_size_t dstoff, bus_size_t count)
{
    __panic_abort();
}

















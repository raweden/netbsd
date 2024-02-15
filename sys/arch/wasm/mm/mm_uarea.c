
#include "mm.h"

/*
 * uvm_uarea_alloc: allocate a u-area
 */

vaddr_t
uvm_uarea_alloc(void)
{
    void *addr = kmem_page_alloc(32, 0);
    if (addr == NULL) {
        printf("%s ERROR no core..\n", __func__);
    } else {
        printf("%s allocated kernel stack {start = %p, end = %p}\n", __func__, addr, addr + (PAGE_SIZE * 32));
    }

    return (vaddr_t)addr;
}

vaddr_t
uvm_uarea_system_alloc(struct cpu_info *ci)
{
    void *addr = kmem_page_alloc(32, 0);
    if (addr == NULL) {
        printf("%s ERROR no core..\n", __func__);
    } else {
        printf("%s allocated kernel stack {start = %p, end = %p}\n", __func__, addr, addr + (PAGE_SIZE * 32));
    }

    return (vaddr_t)addr;
}

/*
 * uvm_uarea_free: free a u-area
 */

void
uvm_uarea_free(vaddr_t uaddr)
{
    void *addr = (void *)uaddr;
    printf("%s freeing kernel stack {start = %p, end = %p}\n", __func__, addr, addr + (PAGE_SIZE * 32));
    kmem_page_free((void *)uaddr, 32);
}

void
uvm_uarea_system_free(vaddr_t uaddr)
{
    void *addr = (void *)uaddr;
    printf("%s freeing kernel stack {start = %p, end = %p}\n", __func__, addr, addr + (PAGE_SIZE * 32));
    kmem_page_free((void *)uaddr, 32);
}

vaddr_t
uvm_lwp_getuarea(lwp_t *l)
{

	return (vaddr_t)l->l_addr - UAREA_PCB_OFFSET;
}

void
uvm_lwp_setuarea(lwp_t *l, vaddr_t addr)
{

	l->l_addr = (void *)(addr + UAREA_PCB_OFFSET);
}
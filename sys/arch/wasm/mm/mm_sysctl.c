
#include <sys/mutex.h>
#include <sys/sysctl.h>

#include <uvm/uvm_extern.h>

#include "mm_private.h"
#include "mm.h"


static struct uvmpdpol_globalstate pdpol_state __cacheline_aligned;


void
uvmpdpol_sysctlsetup(void)
{
	struct uvmpdpol_globalstate *s = &pdpol_state;

	uvm_pctparam_createsysctlnode(&s->s_anonmin, "anonmin",
	    SYSCTL_DESCR("Percentage of physical memory reserved "
	    "for anonymous application data"));
	uvm_pctparam_createsysctlnode(&s->s_filemin, "filemin",
	    SYSCTL_DESCR("Percentage of physical memory reserved "
	    "for cached file data"));
	uvm_pctparam_createsysctlnode(&s->s_execmin, "execmin",
	    SYSCTL_DESCR("Percentage of physical memory reserved "
	    "for cached executable data"));

	uvm_pctparam_createsysctlnode(&s->s_anonmax, "anonmax",
	    SYSCTL_DESCR("Percentage of physical memory which will "
	    "be reclaimed from other usage for "
	    "anonymous application data"));
	uvm_pctparam_createsysctlnode(&s->s_filemax, "filemax",
	    SYSCTL_DESCR("Percentage of physical memory which will "
	    "be reclaimed from other usage for cached "
	    "file data"));
	uvm_pctparam_createsysctlnode(&s->s_execmax, "execmax",
	    SYSCTL_DESCR("Percentage of physical memory which will "
	    "be reclaimed from other usage for cached "
	    "executable data"));

	uvm_pctparam_createsysctlnode(&s->s_inactivepct, "inactivepct",
	    SYSCTL_DESCR("Percentage of inactive queue of "
	    "the entire (active + inactive) queue"));
}
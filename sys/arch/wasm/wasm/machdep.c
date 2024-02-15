/*	$NetBSD: wasm_machdep.c,v 1.29 2023/06/12 19:04:14 skrll Exp $	*/

/*-
 * Copyright (c) 2014, 2019, 2022 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas of 3am Software Foundry, and by Nick Hudson.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

// i386 based start

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: machdep.c,v 1.840 2023/07/16 19:55:43 riastradh Exp $");

#include "opt_beep.h"
#include "opt_compat_freebsd.h"
#include "opt_compat_netbsd.h"
#include "opt_cpureset_delay.h"
#include "opt_ddb.h"
#include "opt_kgdb.h"
#include "opt_mtrr.h"
#include "opt_modular.h"
#include "opt_multiboot.h"
#include "opt_multiprocessor.h"
#include "opt_physmem.h"
#include "opt_realmem.h"
#include "opt_user_ldt.h"
#include "opt_xen.h"
#include "isa.h"
#include "pci.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/signal.h>
#include <sys/signalvar.h>
#include <sys/kernel.h>
#include <sys/cpu.h>
#include <sys/exec.h>
#include <sys/fcntl.h>
#include <sys/reboot.h>
#include <sys/conf.h>
#include <sys/kauth.h>
#include <sys/msgbuf.h>
#include <sys/mount.h>
#include <sys/syscallargs.h>
#include <sys/core.h>
#include <sys/kcore.h>
#include <sys/ucontext.h>
#include <sys/ras.h>
#include <sys/ksyms.h>
#include <sys/device.h>
#include <sys/timevar.h>

#ifdef KGDB
#include <sys/kgdb.h>
#endif

#include <dev/cons.h>
#include <dev/mm.h>

#include <uvm/uvm.h>
#include <uvm/uvm_page.h>

#include <sys/sysctl.h>

#include <wasm/efi.h>

#include <machine/cpu.h>
#include <machine/cpu_rng.h>
#include <machine/cpufunc.h>
#include <machine/cpuvar.h>
#include <machine/gdt.h>
#include <machine/intr.h>
#include <machine/kcore.h>
#include <machine/pio.h>
#include <machine/psl.h>
#include <machine/reg.h>
#include <machine/specialreg.h>
#include <machine/bootinfo.h>
#include <machine/mtrr.h>
#include <machine/pmap_private.h>
#include <wasm/wasm/tsc.h>

#include <wasm/fpu.h>
#include <wasm/dbregs.h>
#include <wasm/machdep.h>

#include <machine/multiboot.h>

#ifdef XEN
#include <xen/evtchn.h>
#include <xen/xen.h>
#include <xen/hypervisor.h>
#endif

#include <dev/isa/isareg.h>
#include <machine/isa_machdep.h>
#include <dev/ic/i8042reg.h>

#include <ddb/db_active.h>

#ifdef DDB
#include <machine/db_machdep.h>
#include <ddb/db_extern.h>
#endif

#include "acpica.h"
#include "bioscall.h"

#if NBIOSCALL > 0
#include <machine/bioscall.h>
#endif

#if NACPICA > 0
#include <dev/acpi/acpivar.h>
#define ACPI_MACHDEP_PRIVATE
#include <machine/acpi_machdep.h>
#else
#include <machine/i82489var.h>
#endif

//#include "isa.h"
//#include "isadma.h"
//#include "ksyms.h"

//#include "cardbus.h"
#if NCARDBUS > 0
/* For rbus_min_start hint. */
#include <sys/bus.h>
#include <dev/cardbus/rbus.h>
#include <machine/rbus_machdep.h>
#endif

#include "mca.h"
#if NMCA > 0
#include <machine/mca_machdep.h>	/* for mca_busprobe() */
#endif

#ifdef MULTIPROCESSOR		/* XXX */
#include <machine/mpbiosvar.h>	/* XXX */
#endif				/* XXX */

/* the following is used externally (sysctl_hw) */
char machine[] = "wasm";			/* CPU "architecture" */
char machine_arch[] = "wasm";		/* machine == machine_arch */

#ifdef CPURESET_DELAY
int cpureset_delay = CPURESET_DELAY;
#else
int cpureset_delay = 2000; /* default to 2s */
#endif

#ifdef MTRR
const struct mtrr_funcs *mtrr_funcs;
#endif

int cpu_class;
int use_pae;
int i386_fpu_fdivbug;

int i386_use_fxsave;
int i386_has_sse;
int i386_has_sse2;

vaddr_t idt_vaddr;
paddr_t idt_paddr;
vaddr_t gdt_vaddr;
paddr_t gdt_paddr;
vaddr_t ldt_vaddr;
paddr_t ldt_paddr;

vaddr_t pentium_idt_vaddr;

extern struct bootspace bootspace;

extern paddr_t lowmem_rsvd;
extern paddr_t avail_start, avail_end;
#ifdef XENPV
extern paddr_t pmap_pa_start, pmap_pa_end;
void hypervisor_callback(void);
void failsafe_callback(void);
#endif

void init386(paddr_t);
void initgdt(union descriptor *);

static void i386_proc0_pcb_ldt_init(void);

int *esym;
int *eblob;
extern int boothowto;

#ifndef XENPV

/* Base memory reported by BIOS. */
#ifndef REALBASEMEM
int biosbasemem = 0;
#else
int biosbasemem = REALBASEMEM;
#endif

/* Extended memory reported by BIOS. */
#ifndef REALEXTMEM
int biosextmem = 0;
#else
int biosextmem = REALEXTMEM;
#endif

/* Set if any boot-loader set biosbasemem/biosextmem. */
int biosmem_implicit;

/*
 * Representation of the bootinfo structure constructed by a NetBSD native
 * boot loader.  Only be used by native_loader().
 */
struct bootinfo_source {
	uint32_t bs_naddrs;
	void *bs_addrs[1]; /* Actually longer. */
};

// static placeholder dummies for some of the i386_trap.S methods
typedef void(*trap_fn)(void);
extern trap_fn wa_exceptions_traps[32];
void wasm_syscall(void);
void wasm_tss_trap08(void);


#endif /* XENPV */

/*
 * Machine-dependent startup code
 */
void
cpu_startup(void)
{
	int x, y;
	vaddr_t minaddr, maxaddr;
	psize_t sz;

	/*
	 * For console drivers that require uvm and pmap to be initialized,
	 * we'll give them one more chance here...
	 */
	consinit();

	/*
	 * Initialize error message buffer (et end of core).
	 */
	if (msgbuf_p_cnt == 0) {
		panic("msgbuf paddr map has not been set up");
	}
	if (msgbuf_vaddr == 0)
		panic("failed to alloc msgbuf_vaddr");
	
	sz = msgbuf_p_cnt * PAGE_SIZE;
	initmsgbuf((void *)msgbuf_vaddr, sz);

#ifdef MULTIBOOT
	multiboot1_print_info();
	multiboot2_print_info();
#endif

#if NCARDBUS > 0
	/* Tell RBUS how much RAM we have, so it can use heuristics. */
	rbus_min_start_hint(ctob((psize_t)physmem));
#endif

	minaddr = 0;

	/* Say hello. */
	banner();

	/* Safe for i/o port / memory space allocation to use malloc now. */
#if NISA > 0 || NPCI > 0
	x86_bus_space_mallocok();
#endif

#ifndef __WASM
	gdt_init();
#endif
	i386_proc0_pcb_ldt_init();

#ifndef __WASM
	cpu_init_tss(&cpu_info_primary);
#endif
#ifndef XENPV
	ltr(cpu_info_primary.ci_tss_sel);
#endif

	x86_startup();
}

/*
 * Set up proc0's PCB and LDT.
 */
static void
i386_proc0_pcb_ldt_init(void)
{
	struct lwp *l = &lwp0;
	struct pcb *pcb = lwp_getpcb(l);

#ifndef __WASM
	pcb->pcb_cr0 = rcr0() & ~CR0_TS;
#else
	pcb->pcb_cr0 = 0;
#endif
	pcb->pcb_esp0 = uvm_lwp_getuarea(l) + USPACE - 16;
	pcb->pcb_iopl = IOPL_KPL;
	l->l_md.md_regs = (struct trapframe *)pcb->pcb_esp0 - 1;
	//memcpy(&pcb->pcb_fsd, &gdtstore[GUDATA_SEL], sizeof(pcb->pcb_fsd));
	//memcpy(&pcb->pcb_gsd, &gdtstore[GUDATA_SEL], sizeof(pcb->pcb_gsd));
	pcb->pcb_dbregs = NULL;

#ifndef XENPV
	lldt(GSEL(GLDT_SEL, SEL_KPL));
#else
	HYPERVISOR_fpu_taskswitch(1);
	HYPERVISOR_stack_switch(GSEL(GDATA_SEL, SEL_KPL), pcb->pcb_esp0);
#endif
}

#ifdef XENPV
/* used in assembly */
void i386_switch_context(lwp_t *);
void i386_tls_switch(lwp_t *);

/*
 * Switch context:
 * - switch stack pointer for user->kernel transition
 */
void
i386_switch_context(lwp_t *l)
{
	struct pcb *pcb;

	pcb = lwp_getpcb(l);

	HYPERVISOR_stack_switch(GSEL(GDATA_SEL, SEL_KPL), pcb->pcb_esp0);

	struct physdev_set_iopl set_iopl;
	set_iopl.iopl = pcb->pcb_iopl;
	HYPERVISOR_physdev_op(PHYSDEVOP_set_iopl, &set_iopl);
}

void
i386_tls_switch(lwp_t *l)
{
	struct cpu_info *ci = curcpu();
	struct pcb *pcb = lwp_getpcb(l);

	/*
	 * Raise the IPL to IPL_HIGH. XXX Still needed?
	 */
	(void)splhigh();

	/* Update TLS segment pointers */
	update_descriptor(&ci->ci_gdt[GUFS_SEL],
	    (union descriptor *)&pcb->pcb_fsd);
	update_descriptor(&ci->ci_gdt[GUGS_SEL],
	    (union descriptor *)&pcb->pcb_gsd);
}
#endif /* XENPV */

/* XXX */
#define IDTVEC(name)	__CONCAT(X, name)
typedef void (vector)(void);

#ifndef XENPV
static void	tss_init(struct i386tss *, void *, void *);

static void
tss_init(struct i386tss *tss, void *stack, void *func)
{
	memset(tss, 0, sizeof *tss);
	tss->tss_esp0 = tss->tss_esp = (int)((char *)stack + USPACE - 16);
	tss->tss_ss0 = GSEL(GDATA_SEL, SEL_KPL);
	tss->__tss_cs = GSEL(GCODE_SEL, SEL_KPL);
	tss->tss_fs = GSEL(GCPU_SEL, SEL_KPL);
	tss->tss_gs = tss->__tss_es = tss->__tss_ds =
	    tss->__tss_ss = GSEL(GDATA_SEL, SEL_KPL);
	/* %cr3 contains the value associated to pmap_kernel */
	tss->tss_cr3 = rcr3();
	tss->tss_esp = (int)((char *)stack + USPACE - 16);
	tss->tss_ldt = GSEL(GLDT_SEL, SEL_KPL);
	tss->__tss_eflags = PSL_MBO | PSL_NT;	/* XXX not needed? */
	tss->__tss_eip = (int)func;
}

extern vector IDTVEC(tss_trap08);
#if defined(DDB) && defined(MULTIPROCESSOR)
extern vector Xintr_ddbipi, Xintr_x2apic_ddbipi;
extern int ddb_vec;
#endif

void
cpu_set_tss_gates(struct cpu_info *ci)
{
#if 0
	struct segment_descriptor sd;
	void *doubleflt_stack;
	idt_descriptor_t *idt;

	doubleflt_stack = (void *)uvm_km_alloc(kernel_map, USPACE, 0,
	    UVM_KMF_WIRED);
	tss_init(&ci->ci_tss->dblflt_tss, doubleflt_stack, wasm_tss_trap08);

	setsegment(&sd, &ci->ci_tss->dblflt_tss, sizeof(struct i386tss) - 1,
	    SDT_SYS386TSS, SEL_KPL, 0, 0);
	ci->ci_gdt[GTRAPTSS_SEL].sd = sd;

	idt = cpu_info_primary.ci_idtvec.iv_idt;
	set_idtgate(&idt[8], NULL, 0, SDT_SYSTASKGT, SEL_KPL,
	    GSEL(GTRAPTSS_SEL, SEL_KPL));

#if defined(DDB) && defined(MULTIPROCESSOR)
	/*
	 * Set up separate handler for the DDB IPI, so that it doesn't
	 * stomp on a possibly corrupted stack.
	 *
	 * XXX overwriting the gate set in db_machine_init.
	 * Should rearrange the code so that it's set only once.
	 */
	void *ddbipi_stack;

	ddbipi_stack = (void *)uvm_km_alloc(kernel_map, USPACE, 0,
	    UVM_KMF_WIRED);
	tss_init(&ci->ci_tss->ddbipi_tss, ddbipi_stack,
	    x2apic_mode ? Xintr_x2apic_ddbipi : Xintr_ddbipi);

	setsegment(&sd, &ci->ci_tss->ddbipi_tss, sizeof(struct i386tss) - 1,
	    SDT_SYS386TSS, SEL_KPL, 0, 0);
	ci->ci_gdt[GIPITSS_SEL].sd = sd;

	set_idtgate(&idt[ddb_vec], NULL, 0, SDT_SYSTASKGT, SEL_KPL,
	    GSEL(GIPITSS_SEL, SEL_KPL));
#endif
#endif
}
#endif /* XENPV */

/*
 * Set up TSS and I/O bitmap.
 */
void
cpu_init_tss(struct cpu_info *ci)
{
	struct cpu_tss *cputss;

	cputss = (struct cpu_tss *)kmem_zalloc(sizeof(struct cpu_tss), 0);

	cputss->tss.tss_iobase = IOMAP_INVALOFF << 16;
#ifndef XENPV
	cputss->tss.tss_ss0 = GSEL(GDATA_SEL, SEL_KPL);
	cputss->tss.tss_ldt = GSEL(GLDT_SEL, SEL_KPL);
	cputss->tss.tss_cr3 = rcr3();
#endif

	ci->ci_tss = cputss;
#ifndef XENPV
	ci->ci_tss_sel = tss_alloc(&cputss->tss);
#endif
}

void *
getframe(struct lwp *l, int sig, int *onstack)
{
	struct proc *p = l->l_proc;
	struct trapframe *tf = l->l_md.md_regs;

	/* Do we need to jump onto the signal stack? */
	*onstack = (l->l_sigstk.ss_flags & (SS_DISABLE | SS_ONSTACK)) == 0
	    && (SIGACTION(p, sig).sa_flags & SA_ONSTACK) != 0;
	if (*onstack)
		return (char *)l->l_sigstk.ss_sp + l->l_sigstk.ss_size;
	return (void *)tf->tf_esp;
}

/*
 * Build context to run handler in.  We invoke the handler
 * directly, only returning via the trampoline.  Note the
 * trampoline version numbers are coordinated with machine-
 * dependent code in libc.
 */
void
buildcontext(struct lwp *l, int sel, void *catcher, void *fp)
{
	struct trapframe *tf = l->l_md.md_regs;

	tf->tf_gs = GSEL(GUGS_SEL, SEL_UPL);
	tf->tf_fs = GSEL(GUFS_SEL, SEL_UPL);
	tf->tf_es = GSEL(GUDATA_SEL, SEL_UPL);
	tf->tf_ds = GSEL(GUDATA_SEL, SEL_UPL);
	tf->tf_eip = (int)catcher;
	tf->tf_cs = GSEL(sel, SEL_UPL);
	tf->tf_eflags &= ~PSL_CLEARSIG;
	tf->tf_esp = (int)fp;
	tf->tf_ss = GSEL(GUDATA_SEL, SEL_UPL);

	/* Ensure FP state is reset. */
	fpu_sigreset(l);
}

void
sendsig_siginfo(const ksiginfo_t *ksi, const sigset_t *mask)
{
	struct lwp *l = curlwp;
	struct proc *p = l->l_proc;
	struct pmap *pmap = vm_map_pmap(&p->p_vmspace->vm_map);
	int sel = pmap->pm_hiexec > I386_MAX_EXE_ADDR ?
	    GUCODEBIG_SEL : GUCODE_SEL;
	struct sigacts *ps = p->p_sigacts;
	int onstack, error;
	int sig = ksi->ksi_signo;
	struct sigframe_siginfo *fp = getframe(l, sig, &onstack), frame;
	sig_t catcher = SIGACTION(p, sig).sa_handler;

	KASSERT(mutex_owned(p->p_lock));

	fp--;

	memset(&frame, 0, sizeof(frame));
	frame.sf_ra = (int)ps->sa_sigdesc[sig].sd_tramp;
	frame.sf_signum = sig;
	frame.sf_sip = &fp->sf_si;
	frame.sf_ucp = &fp->sf_uc;
	frame.sf_si._info = ksi->ksi_info;
	frame.sf_uc.uc_flags = _UC_SIGMASK|_UC_VM;
	frame.sf_uc.uc_sigmask = *mask;
	frame.sf_uc.uc_link = l->l_ctxlink;
	frame.sf_uc.uc_flags |= (l->l_sigstk.ss_flags & SS_ONSTACK)
	    ? _UC_SETSTACK : _UC_CLRSTACK;

	sendsig_reset(l, sig);

	mutex_exit(p->p_lock);
	cpu_getmcontext(l, &frame.sf_uc.uc_mcontext, &frame.sf_uc.uc_flags);
	error = copyout(&frame, fp, sizeof(frame));
	mutex_enter(p->p_lock);

	if (error != 0) {
		/*
		 * Process has trashed its stack; give it an illegal
		 * instruction to halt it in its tracks.
		 */
		sigexit(l, SIGILL);
		/* NOTREACHED */
	}

	buildcontext(l, sel, catcher, fp);

	/* Remember that we're now on the signal stack. */
	if (onstack)
		l->l_sigstk.ss_flags |= SS_ONSTACK;
}

static void
maybe_dump(int howto)
{
	int s;

	/* Disable interrupts. */
	s = splhigh();

	/* Do a dump if requested. */
	if ((howto & (RB_DUMP | RB_HALT)) == RB_DUMP)
		dumpsys();

	splx(s);
}

void
cpu_reboot(int howto, char *bootstr)
{
	static bool syncdone = false;
	int s = IPL_NONE;

	if (cold) {
		howto |= RB_HALT;
		goto haltsys;
	}

	boothowto = howto;

	/* XXX used to dump after vfs_shutdown() and before
	 * detaching devices / shutdown hooks / pmf_system_shutdown().
	 */
	maybe_dump(howto);

	/*
	 * If we've panic'd, don't make the situation potentially
	 * worse by syncing or unmounting the file systems.
	 */
	if ((howto & RB_NOSYNC) == 0 && panicstr == NULL) {
		if (!syncdone) {
			syncdone = true;
			/* XXX used to force unmount as well, here */
			vfs_sync_all(curlwp);
			/*
			 * If we've been adjusting the clock, the todr
			 * will be out of synch; adjust it now.
			 *
			 * XXX used to do this after unmounting all
			 * filesystems with vfs_shutdown().
			 */
			if (time_adjusted != 0)
				resettodr();
		}

		while (vfs_unmountall1(curlwp, false, false) ||
		       config_detach_all(boothowto) ||
		       vfs_unmount_forceone(curlwp))
			;	/* do nothing */
	} else {
		if (!db_active)
			suspendsched();
	}

	pmf_system_shutdown(boothowto);

	s = splhigh();

	/* amd64 maybe_dump() */

haltsys:
	doshutdownhooks();

	if ((howto & RB_POWERDOWN) == RB_POWERDOWN) {
#if NACPICA > 0
		if (s != IPL_NONE)
			splx(s);

		acpi_enter_sleep_state(ACPI_STATE_S5);
#else
		__USE(s);
#endif
#ifdef XEN
		if (vm_guest == VM_GUEST_XENPV ||
		    vm_guest == VM_GUEST_XENPVH ||
		    vm_guest == VM_GUEST_XENPVHVM)
			HYPERVISOR_shutdown();
#endif /* XEN */
	}

#ifdef MULTIPROCESSOR
	cpu_broadcast_halt();
#endif /* MULTIPROCESSOR */

	if (howto & RB_HALT) {
#if NACPICA > 0
		acpi_disable();
#endif

		printf("\n");
		printf("The operating system has halted.\n");
		printf("Please press any key to reboot.\n\n");

#ifdef BEEP_ONHALT
		{
			int c;
			for (c = BEEP_ONHALT_COUNT; c > 0; c--) {
				sysbeep(BEEP_ONHALT_PITCH,
					BEEP_ONHALT_PERIOD * hz / 1000);
				delay(BEEP_ONHALT_PERIOD * 1000);
				sysbeep(0, BEEP_ONHALT_PERIOD * hz / 1000);
				delay(BEEP_ONHALT_PERIOD * 1000);
			}
		}
#endif

		cnpollc(1);	/* for proper keyboard command handling */
		if (cngetc() == 0) {
			/* no console attached, so just hlt */
			printf("No keyboard - cannot reboot after all.\n");
			for(;;) {
				x86_hlt();
			}
		}
		cnpollc(0);
	}

	printf("rebooting...\n");
	if (cpureset_delay > 0)
		delay(cpureset_delay * 1000);
	cpu_reset();
	for(;;) ;
	/*NOTREACHED*/
}

/*
 * Clear registers on exec
 */
void
setregs(struct lwp *l, struct exec_package *pack, vaddr_t stack)
{
	struct pmap *pmap = vm_map_pmap(&l->l_proc->p_vmspace->vm_map);
	struct pcb *pcb = lwp_getpcb(l);
	struct trapframe *tf;

#ifdef USER_LDT
	pmap_ldt_cleanup(l);
#endif

	fpu_clear(l, pack->ep_osversion >= 699002600
	    ? __INITIAL_NPXCW__ : __NetBSD_COMPAT_NPXCW__);

	memcpy(&pcb->pcb_fsd, &gdtstore[GUDATA_SEL], sizeof(pcb->pcb_fsd));
	memcpy(&pcb->pcb_gsd, &gdtstore[GUDATA_SEL], sizeof(pcb->pcb_gsd));

	// wasm? x86_dbregs_clear(l);

	tf = l->l_md.md_regs;
	tf->tf_gs = GSEL(GUGS_SEL, SEL_UPL);
	tf->tf_fs = GSEL(GUFS_SEL, SEL_UPL);
	tf->tf_es = LSEL(LUDATA_SEL, SEL_UPL);
	tf->tf_ds = LSEL(LUDATA_SEL, SEL_UPL);
	tf->tf_edi = 0;
	tf->tf_esi = 0;
	tf->tf_ebp = 0;
	tf->tf_ebx = l->l_proc->p_psstrp;
	tf->tf_edx = 0;
	tf->tf_ecx = 0;
	tf->tf_eax = 0;
	tf->tf_eip = pack->ep_entry;
	tf->tf_cs = pmap->pm_hiexec > I386_MAX_EXE_ADDR ?
	    LSEL(LUCODEBIG_SEL, SEL_UPL) : LSEL(LUCODE_SEL, SEL_UPL);
	tf->tf_eflags = PSL_USERSET;
	tf->tf_esp = stack;
	tf->tf_ss = LSEL(LUDATA_SEL, SEL_UPL);
}

/*
 * Initialize segments and descriptor tables
 */

union descriptor *gdtstore, *ldtstore;
union descriptor *pentium_idt;
extern vaddr_t lwp0uarea;

void
setgate(struct gate_descriptor *gd, void *func, int args, int type, int dpl,
    int sel)
{

	gd->gd_looffset = (int)func;
	gd->gd_selector = sel;
	gd->gd_stkcpy = args;
	gd->gd_xx = 0;
	gd->gd_type = type;
	gd->gd_dpl = dpl;
	gd->gd_p = 1;
	gd->gd_hioffset = (int)func >> 16;
}

void
unsetgate(struct gate_descriptor *gd)
{

	gd->gd_p = 0;
	gd->gd_hioffset = 0;
	gd->gd_looffset = 0;
	gd->gd_selector = 0;
	gd->gd_xx = 0;
	gd->gd_stkcpy = 0;
	gd->gd_type = 0;
	gd->gd_dpl = 0;
}

void
setregion(struct region_descriptor *rd, void *base, size_t limit)
{

	rd->rd_limit = (int)limit;
	rd->rd_base = (int)base;
}

void
setsegment(struct segment_descriptor *sd, const void *base, size_t limit,
    int type, int dpl, int def32, int gran)
{

	sd->sd_lolimit = (int)limit;
	sd->sd_lobase = (int)base;
	sd->sd_type = type;
	sd->sd_dpl = dpl;
	sd->sd_p = 1;
	sd->sd_hilimit = (int)limit >> 16;
	sd->sd_xx = 0;
	sd->sd_def32 = def32;
	sd->sd_gran = gran;
	sd->sd_hibase = (int)base >> 24;
}

/* XXX */
extern vector IDTVEC(syscall);
#ifdef XENPV
extern union descriptor tmpgdt[];
#endif

void
cpu_init_idt(struct cpu_info *ci)
{
	struct region_descriptor region;
	struct idt_vec *iv;
	idt_descriptor_t *idt;

	iv = &ci->ci_idtvec;
	idt = iv->iv_idt_pentium;
	setregion(&region, idt, NIDT * sizeof(idt[0]) - 1);
	lidt(&region);
}


#if !defined(XENPV)  && NBIOSCALL > 0
static void
init386_pte0(void)
{
	paddr_t paddr;
	vaddr_t vaddr;

	paddr = 4 * PAGE_SIZE;
	vaddr = (vaddr_t)vtopte(0);
	pmap_kenter_pa(vaddr, paddr, VM_PROT_ALL, 0);
	pmap_update(pmap_kernel());
	/* make sure it is clean before using */
	memset((void *)vaddr, 0, PAGE_SIZE);
}
#endif /* !XENPV && NBIOSCALL > 0 */

#ifndef XENPV
static void
init386_ksyms(void)
{
#if NKSYMS || defined(DDB) || defined(MODULAR)
	extern int end;
	struct btinfo_symtab *symtab;

#ifdef DDB
	db_machine_init();
#endif

#if defined(MULTIBOOT)
	if (multiboot1_ksyms_addsyms_elf())
		return;

	if (multiboot2_ksyms_addsyms_elf())
		return;
#endif

	if ((symtab = lookup_bootinfo(BTINFO_SYMTAB)) == NULL) {
		ksyms_addsyms_elf(*(int *)&end, ((int *)&end) + 1, esym);
		return;
	}

	symtab->ssym += KERNBASE;
	symtab->esym += KERNBASE;
	ksyms_addsyms_elf(symtab->nsym, (int *)symtab->ssym, (int *)symtab->esym);
#endif
}
#endif /* XENPV */


// initlize early console (prints to console.log for WebAssembly)

// early console

int __kcons_write(const char *buf, unsigned int bufsz, unsigned int flags, unsigned int level) __WASM_IMPORT(kern, cons_write);

extern void (*v_putc)(int);		/* start with cnputc (normal cons) */
extern void (*v_flush)(void);	/* start with cnflush (normal cons) */

#define __CNBUFSZ 512
static char cnbuf[__CNBUFSZ];
static short cnlen = 0;

static void
wasm_cnflush(void)
{
	if (cnlen == 0)
		return;
	
	__kcons_write(cnbuf, cnlen, 0, 0);
	cnlen = 0;
}

static void
wasm_cnputc(int c)
{
	cnbuf[cnlen++] = c;
	if (c == '\n' || cnlen == (__CNBUFSZ - 1)) {
		wasm_cnflush();
	}
}



#undef __CNBUFSZ

static void
init_earlycons(void)
{
	/* set temporally to work printf()/panic() even before consinit() */
	//cn_tab = &earlycons;

	// TODO: wasm, this will only work as long as we only got one thread in kernel space..
	// low level hack.
	v_putc = wasm_cnputc;
	v_flush = wasm_cnflush;
}

void wasm_fixup_physseg(void);
void init_wasm_memory(void);

void
init_wasm32(paddr_t first_avail)
{
	extern void consinit(void);
	int x;
#ifndef XENPV
	extern paddr_t local_apic_pa;
	union descriptor *tgdt;
	struct region_descriptor region;
#if NBIOSCALL > 0
	extern int biostramp_image_size;
	extern u_char biostramp_image[];
#endif
#endif /* !XENPV */
	struct pcb *pcb;
	struct idt_vec *iv;
	idt_descriptor_t *idt;

	KASSERT(first_avail % PAGE_SIZE == 0);

#ifdef XENPV
	KASSERT(HYPERVISOR_shared_info != NULL);
	cpu_info_primary.ci_vcpu = &HYPERVISOR_shared_info->vcpu_info[0];
#endif

#ifdef XEN
	if (vm_guest == VM_GUEST_XENPVH)
		xen_parse_cmdline(XEN_PARSE_BOOTFLAGS, NULL);
#endif

	// initilize simple hocks to printing to console.
	init_earlycons();

	init_wasm_memory();

	uvm_lwp_setuarea(&lwp0, lwp0uarea);

	cpu_probe(&cpu_info_primary);

	/*
	 * Initialize the no-execute bit on cpu0, if supported.
	 *
	 * Note: The call to cpu_init_msrs for secondary CPUs happens
	 * in cpu_hatch.
	 */
	cpu_init_msrs(&cpu_info_primary, true);

#ifndef XENPV
	cpu_speculation_init(&cpu_info_primary);
#endif

#ifdef PAE
	use_pae = 1;
#else
	use_pae = 0;
#endif

	pcb = lwp_getpcb(&lwp0);
#ifdef XENPV
	pcb->pcb_cr3 = PDPpaddr;
#endif

#if defined(PAE) && !defined(XENPV)
	/*
	 * Save VA and PA of L3 PD of boot processor (for Xen, this is done
	 * in xen_locore())
	 */
	cpu_info_primary.ci_pae_l3_pdirpa = rcr3();
	cpu_info_primary.ci_pae_l3_pdir = (pd_entry_t *)(rcr3() + KERNBASE);
#endif

	/*
	 * Start with 2 color bins -- this is just a guess to get us
	 * started.  We'll recolor when we determine the largest cache
	 * sizes on the system.
	 */
	uvmexp.ncolors = 2;

	//avail_start = first_avail;

	/*
	 * Low memory reservations:
	 * Page 0:	BIOS data
	 * Page 1:	BIOS callback
	 * Page 2:	MP bootstrap code (MP_TRAMPOLINE)
	 * Page 3:	ACPI wakeup code (ACPI_WAKEUP_ADDR)
	 * Page 4:	Temporary page table for 0MB-4MB
	 * Page 5:	Temporary page directory
	 */
	//lowmem_rsvd = 6 * PAGE_SIZE;
	//lowmem_rsvd = 0;

#if NISA > 0 || NPCI > 0
	x86_bus_space_init();
#endif


	consinit();	/* XXX SHOULD NOT BE DONE HERE */

	/*
	 * Initialize RNG to get entropy ASAP either from CPU
	 * RDRAND/RDSEED or from seed on disk.  Must happen after
	 * cpu_init_msrs.  Prefer to happen after consinit so we have
	 * the opportunity to print useful feedback.
	 */
	cpu_rng_init();
	x86_rndseed();


#if !defined(XENPV) && NBIOSCALL > 0
	/*
	 * XXX Remove this
	 *
	 * Setup a temporary Page Table Entry to allow identity mappings of
	 * the real mode address. This is required by bioscall.
	 */
	init386_pte0();

	KASSERT(biostramp_image_size <= PAGE_SIZE);
	pmap_kenter_pa((vaddr_t)BIOSTRAMP_BASE, (paddr_t)BIOSTRAMP_BASE,
	    VM_PROT_ALL, 0);
	pmap_update(pmap_kernel());
	memcpy((void *)BIOSTRAMP_BASE, biostramp_image, biostramp_image_size);

	/* Needed early, for bioscall() */
	cpu_info_primary.ci_pmap = pmap_kernel();
#endif

#ifndef XENPV
	/*
	 * Activate the second temporary GDT, allocated in
	 * pmap_bootstrap with pmap_bootstrap_valloc/palloc, and
	 * initialized with the content of the initial temporary GDT in
	 * initgdt, plus an updated LDT.
	 *
	 * This ensures the %fs-relative addressing for the CPU-local
	 * area used by CPUVAR(...), curcpu(), and curlwp will continue
	 * to work after init386 returns and the initial temporary GDT
	 * is popped off, before we call main and later create a
	 * permanent GDT in gdt_init via cpu_startup.
	 */
	setregion(&region, gdtstore, NGDT * sizeof(gdtstore[0]) - 1);
	lgdt(&region);
#endif

	lldt(GSEL(GLDT_SEL, SEL_KPL));
	cpu_init_idt(&cpu_info_primary);

#ifdef XENPV
	xen_init_ksyms();
#else /* XENPV */
#ifdef XEN
	if (vm_guest == VM_GUEST_XENPVH)
		xen_init_ksyms();
	else
#endif /* XEN */
		init386_ksyms();
#endif /* XENPV */

#if NMCA > 0
	/* 
	 * check for MCA bus, needed to be done before ISA stuff - if
	 * MCA is detected, ISA needs to use level triggered interrupts
	 * by default
	 * And we do not search for MCA using bioscall() on EFI systems
	 * that lacks it (they lack MCA too, anyway).
	 */
	if (lookup_bootinfo(BTINFO_EFI) == NULL && vm_guest != VM_GUEST_XENPVH)
		mca_busprobe();
#endif

#ifdef XENPV
	extern int tmpstk;
	cpu_info_primary.ci_intrstack = &tmpstk;
	events_default_setup();
#else
	intr_default_setup();
#endif

	splraise(IPL_HIGH);
	x86_enable_intr();

#ifdef DDB
	if (boothowto & RB_KDB)
		Debugger();
#endif
#ifdef KGDB
	kgdb_port_init();
	if (boothowto & RB_KDB) {
		kgdb_debug_init = 1;
		kgdb_connect(1);
	}
#endif

	if (physmem < btoc(2 * 1024 * 1024)) {
		printf("warning: too little memory available; "
		       "have %lu bytes, want %lu bytes\n"
		       "running in degraded mode\n"
		       "press a key to confirm\n\n",
		       (unsigned long)ptoa(physmem), 2*1024*1024UL);
		cngetc();
	}

	pcb->pcb_dbregs = NULL;
	// wasm? x86_dbregs_init();
}

#include <dev/ic/mc146818reg.h>		/* for NVRAM POST */
#include <wasm/isa/nvram.h>		/* for NVRAM POST */

void
cpu_reset(void)
{
#ifdef XENPV
	HYPERVISOR_reboot();
	for (;;);
#else /* XENPV */
	struct region_descriptor region;
	idt_descriptor_t *idt;

	idt = (idt_descriptor_t *)cpu_info_primary.ci_idtvec.iv_idt;
	x86_disable_intr();

	/*
	 * Ensure the NVRAM reset byte contains something vaguely sane.
	 */

	outb(IO_RTC, NVRAM_RESET);
	outb(IO_RTC+1, NVRAM_RESET_RST);

	/*
	 * Reset AMD Geode SC1100.
	 *
	 * 1) Write PCI Configuration Address Register (0xcf8) to
	 *    select Function 0, Register 0x44: Bridge Configuration,
	 *    GPIO and LPC Configuration Register Space, Reset
	 *    Control Register.
	 *
	 * 2) Write 0xf to PCI Configuration Data Register (0xcfc)
	 *    to reset IDE controller, IDE bus, and PCI bus, and
	 *    to trigger a system-wide reset.
	 *
	 * See AMD Geode SC1100 Processor Data Book, Revision 2.0,
	 * sections 6.3.1, 6.3.2, and 6.4.1.
	 */
	if (cpu_info_primary.ci_signature == 0x540) {
		outl(0xcf8, 0x80009044);
		outl(0xcfc, 0xf);
	}

	x86_reset();

	/*
	 * Try to cause a triple fault and watchdog reset by making the IDT
	 * invalid and causing a fault.
	 */
	memset((void *)idt, 0, NIDT * sizeof(idt[0]));
	setregion(&region, idt, NIDT * sizeof(idt[0]) - 1);
	lidt(&region);
	breakpoint();

#if 0
	/*
	 * Try to cause a triple fault and watchdog reset by unmapping the
	 * entire address space and doing a TLB flush.
	 */
	memset((void *)PTD, 0, PAGE_SIZE);
	tlbflush();
#endif

	for (;;);
#endif /* XENPV */
}

void
cpu_getmcontext(struct lwp *l, mcontext_t *mcp, unsigned int *flags)
{
	const struct trapframe *tf = l->l_md.md_regs;
	__greg_t *gr = mcp->__gregs;
	__greg_t ras_eip;

	/* Save register context. */
	gr[_REG_GS]  = tf->tf_gs;
	gr[_REG_FS]  = tf->tf_fs;
	gr[_REG_ES]  = tf->tf_es;
	gr[_REG_DS]  = tf->tf_ds;
	gr[_REG_EFL] = tf->tf_eflags;

	gr[_REG_EDI]    = tf->tf_edi;
	gr[_REG_ESI]    = tf->tf_esi;
	gr[_REG_EBP]    = tf->tf_ebp;
	gr[_REG_EBX]    = tf->tf_ebx;
	gr[_REG_EDX]    = tf->tf_edx;
	gr[_REG_ECX]    = tf->tf_ecx;
	gr[_REG_EAX]    = tf->tf_eax;
	gr[_REG_EIP]    = tf->tf_eip;
	gr[_REG_CS]     = tf->tf_cs;
	gr[_REG_ESP]    = tf->tf_esp;
	gr[_REG_UESP]   = tf->tf_esp;
	gr[_REG_SS]     = tf->tf_ss;
	gr[_REG_TRAPNO] = tf->tf_trapno;
	gr[_REG_ERR]    = tf->tf_err;

	if ((ras_eip = (__greg_t)ras_lookup(l->l_proc,
	    (void *) gr[_REG_EIP])) != -1)
		gr[_REG_EIP] = ras_eip;

	*flags |= _UC_CPU;

	mcp->_mc_tlsbase = (uintptr_t)l->l_private;
	*flags |= _UC_TLSBASE;

	/*
	 * Save floating point register context.
	 *
	 * If the cpu doesn't support fxsave we must still write to
	 * the entire 512 byte area - otherwise we leak kernel memory
	 * contents to userspace.
	 * It wouldn't matter if we were doing the copyout here.
	 * So we might as well convert to fxsave format.
	 */
	__CTASSERT(sizeof (struct fxsave) ==
	    sizeof mcp->__fpregs.__fp_reg_set.__fp_xmm_state);
	process_read_fpregs_xmm(l, (struct fxsave *)
	    &mcp->__fpregs.__fp_reg_set.__fp_xmm_state);
	memset(&mcp->__fpregs.__fp_pad, 0, sizeof mcp->__fpregs.__fp_pad);
	*flags |= _UC_FXSAVE | _UC_FPU;
}

int
cpu_mcontext_validate(struct lwp *l, const mcontext_t *mcp)
{
	const __greg_t *gr = mcp->__gregs;
	struct trapframe *tf = l->l_md.md_regs;

	/*
	 * Check for security violations.  If we're returning
	 * to protected mode, the CPU will validate the segment
	 * registers automatically and generate a trap on
	 * violations.  We handle the trap, rather than doing
	 * all of the checking here.
	 */
	if (((gr[_REG_EFL] ^ tf->tf_eflags) & PSL_USERSTATIC) ||
	    !USERMODE(gr[_REG_CS]))
		return EINVAL;

	return 0;
}

int
cpu_setmcontext(struct lwp *l, const mcontext_t *mcp, unsigned int flags)
{
	struct trapframe *tf = l->l_md.md_regs;
	const __greg_t *gr = mcp->__gregs;
	struct proc *p = l->l_proc;
	int error;

	/* Restore register context, if any. */
	if ((flags & _UC_CPU) != 0) {
		error = cpu_mcontext_validate(l, mcp);
		if (error)
			return error;

		tf->tf_gs = gr[_REG_GS];
		tf->tf_fs = gr[_REG_FS];
		tf->tf_es = gr[_REG_ES];
		tf->tf_ds = gr[_REG_DS];
		/* Only change the user-alterable part of eflags */
		tf->tf_eflags &= ~PSL_USER;
		tf->tf_eflags |= (gr[_REG_EFL] & PSL_USER);

		tf->tf_edi    = gr[_REG_EDI];
		tf->tf_esi    = gr[_REG_ESI];
		tf->tf_ebp    = gr[_REG_EBP];
		tf->tf_ebx    = gr[_REG_EBX];
		tf->tf_edx    = gr[_REG_EDX];
		tf->tf_ecx    = gr[_REG_ECX];
		tf->tf_eax    = gr[_REG_EAX];
		tf->tf_eip    = gr[_REG_EIP];
		tf->tf_cs     = gr[_REG_CS];
		tf->tf_esp    = gr[_REG_UESP];
		tf->tf_ss     = gr[_REG_SS];
	}

	if ((flags & _UC_TLSBASE) != 0)
		lwp_setprivate(l, (void *)(uintptr_t)mcp->_mc_tlsbase);

	/* Restore floating point register context, if given. */
	if ((flags & _UC_FPU) != 0) {
		__CTASSERT(sizeof (struct fxsave) ==
		    sizeof mcp->__fpregs.__fp_reg_set.__fp_xmm_state);
		__CTASSERT(sizeof (struct save87) ==
		    sizeof mcp->__fpregs.__fp_reg_set.__fpchip_state);

		if (flags & _UC_FXSAVE) {
			process_write_fpregs_xmm(l, (const struct fxsave *)
				    &mcp->__fpregs.__fp_reg_set.__fp_xmm_state);
		} else {
			process_write_fpregs_s87(l, (const struct save87 *)
				    &mcp->__fpregs.__fp_reg_set.__fpchip_state);
		}
	}

	mutex_enter(p->p_lock);
	if (flags & _UC_SETSTACK)
		l->l_sigstk.ss_flags |= SS_ONSTACK;
	if (flags & _UC_CLRSTACK)
		l->l_sigstk.ss_flags &= ~SS_ONSTACK;
	mutex_exit(p->p_lock);
	return (0);
}

#define	DEV_IO 14		/* iopl for compat_10 */

int
mm_md_open(dev_t dev, int flag, int mode, struct lwp *l)
{

	switch (minor(dev)) {
	case DEV_IO:
		/*
		 * This is done by i386_iopl(3) now.
		 *
		 * #if defined(COMPAT_10) || defined(COMPAT_FREEBSD)
		 */
		if (flag & FWRITE) {
			struct trapframe *fp;
			int error;

			error = kauth_authorize_machdep(l->l_cred,
			    KAUTH_MACHDEP_IOPL, NULL, NULL, NULL, NULL);
			if (error)
				return (error);
			fp = curlwp->l_md.md_regs;
			fp->tf_eflags |= PSL_IOPL;
		}
		break;
	default:
		break;
	}
	return 0;
}

static void
idt_vec_copy(struct idt_vec *dst, struct idt_vec *src)
{
	idt_descriptor_t *idt_dst;

	idt_dst = dst->iv_idt;
	memcpy(idt_dst, src->iv_idt, PAGE_SIZE);
	memcpy(dst->iv_allocmap, src->iv_allocmap, sizeof(dst->iv_allocmap));
}

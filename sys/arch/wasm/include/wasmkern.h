


extern char **__wasmkern_envp;

int wasmkern_getparam(const char *, void *, size_t);

extern struct vmspace *rump_vmspace_local;
extern struct pmap rump_pmap_local;
#define RUMP_LOCALPROC_P(p) \
    (p->p_vmspace == vmspace_kernel() || p->p_vmspace == rump_vmspace_local)

/* vm bundle for remote clients.  the last member is the hypercall cookie */
struct rump_spctl {
	struct vmspace spctl_vm;
	void *spctl;
};
#define RUMP_SPVM2CTL(vm) (((struct rump_spctl *)vm)->spctl)
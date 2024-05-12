

#include <sys/stdint.h> // uintX_t

#ifndef _WASM_WASMEXEC_H_
#define _WASM_WASMEXEC_H_

struct ps_strings;

// a new header for wasm-executable, this should be placed in a custom wasm section as the first section,
// points are encoded as relative to wash_exechdr_rt start, addr 0 (aka top) is NULL and these needs to be
// relocated to be correctly read in.

// NOTE: ATM this is experimental (but required) and changes are likley to be made.

#define RTLD_ET_EXTERN_MAIN_ENTRYPOINT 1
#define RTLD_ET_EXEC_RTLD 2
#define RTLD_ET_EXEC_START 3

#define WASM_EXEC_PKG_SIGN 0x77614550        // waEP

struct wasm32_execpkg_args {
    uint32_t ep_size;   // size of structure in bytes.
    uint32_t et_sign;   // 
    uint8_t et_type;
    // inline struct ps_strings
	char	**ps_argvstr;	/* first of 0 or more argument strings */
	int	ps_nargvstr;	    /* the number of argument strings */
	char	**ps_envstr;	/* first of 0 or more environment strings */
	int	ps_nenvstr;	        /* the number of environment strings */
    //uint32_t ps_nenvstr;
    //uint32_t ps_nargvstr;
    // char *ps_argvstr[ps_nargvstr];       // NULL terminated
    // char *ps_argvstr[ps_nenvstr];        // NULL terminated
    // char *strings[]
    struct ps_strings *ps_addr;              // ps_strings in user-space
    int (*rtld)(uintptr_t *, uintptr_t);
    int (*__start)(void(*)(void), struct ps_strings *);
};

struct wasm_exechdr_secinfo { 
	uint32_t file_offset;
	uint32_t sec_size;
	const char *name;
    uint8_t namesz;         // names longer than 255 chars are truncated to 255 chars.
	uint8_t hdrsz;          // size of (u8 section-type + uleb section-size) for faster reloc
	uint8_t wasm_type;
	uint8_t flags;          // or role
};

struct wash_exechdr_rt {
    uint32_t hdr_size;                       // size of the whole header (including trailing string-table + secinfo)
    uint32_t hdr_version;
    uint32_t exec_type;
    uint32_t exec_traits;                   // bitwise flags
    uint32_t stack_size_hint;
    int32_t exec_start_elemidx;             // TODO: put in dylink.0
    int32_t exec_start_funcidx;
    uint16_t runtime_abi_traits;            // runtime abi is a adhoc solution to support more than one worker wrapper.
    uint16_t runtime_abisz;
    const char *runtime_abi;
    uint32_t section_cnt;
    struct wasm_exechdr_secinfo *secdata;   // of length section_cnt
};


// this one should be in its own header
struct wasm_loader_args {
    uint32_t exec_flags;
    uint32_t exec_end_offset;
    uint32_t stack_size_hint;
    uint32_t ep_resolvednamesz;
    char *ep_resolvedname;
    struct wash_exechdr_rt *ep_exechdr;
};

struct dlsym_rt {
    const char *name;
    unsigned long addr;
    unsigned short namesz;
    unsigned char flags;
    unsigned char type;
};

struct rtld_segment {
    uint16_t flags;
    uint16_t align;
    uint8_t type;
    uint8_t namesz;
    const char *name;
    uintptr_t addr;
    uintptr_t size;
    uint32_t src_offset;    // might not be needed (since address is NULL until its location is determined, until then src could be stored there)
};

struct rtld_memory_descriptor {
    uint8_t shared;
    uint8_t flags;
    uint8_t module_namesz;
    uint8_t namesz;
    const char *module_name;
    const char *name;
    int32_t initial;        // -1 indicates not set
    int32_t maximum;        // -1 indicates not set
};

struct _rtld_search_path {
	struct _rtld_search_path *sp_next;
	const char     *sp_path;
	uint32_t        sp_pathlen;
};

struct _rtld_needed_entry {
    struct _rtld_needed_entry *next;
    struct wasm_module_rt *obj;     // the shared object which is the dependency (not the dependant)
    uint8_t namesz;
    uint8_t verssz;
    const char *name;
    const char *vers;
};

/**
 * This is the structure that represents the module data at runtime.
 */
struct wasm_module_rt {
    struct wasm_module_rt *next;
    uint32_t flags;
    uint32_t ld_state;
    int32_t ld_refcount;
    int32_t fd;
    const char *filepath;
    struct _rtld_search_path *rpaths;
    struct rtld_memory_descriptor *memdesc;  // TODO: need support for replicating more complex multi-memory layout
    struct dlsym *dlsym_start;
    struct dlsym *dlsym_end;
    struct dlsym *dlsym_func_start;
    struct dlsym *dlsym_func_end;
    struct dlsym *dlsym_data_start;
    struct dlsym *dlsym_data_end;
    uint16_t pre_init_array_count;
    uint16_t init_array_count;
    uint16_t fnit_array_count;
    void (*pre_init_array)(void);
    void (*init_array)(void);
    void (*fnit_array)(void);
    void (*__start)(void(*)(void), struct ps_strings *);
    uint16_t data_segments_count;
    uint16_t elem_segments_count;
    struct rtld_segment *data_segments;
    struct rtld_segment *elem_segments;
    struct wash_exechdr_rt *exechdr;        // holds section map etc.
    struct _rtld_needed_entry *needed;
    uint16_t dso_pathlen;
    uint8_t dso_namesz;
    uint8_t dso_verssz;
    const char *dso_name;
    const char *dso_vers;
    uint64_t ld_dev;
    uint64_t ld_ino;
    uintptr_t membase;
    uintptr_t memsize;
    uintptr_t tblbase;
    uintptr_t tblend;
    int obj_execdesc;       // like a fd that links against a object in JavaScript land that holds reference to the WebAssembly.Instance
};

struct ldwasm_aux {
    uint32_t a_type;
    uint32_t a_val;
};

#define LDWASM_AT_NULL 0
#define LDWASM_AT_EXECFD 2
#define LDWASM_AT_PAGESZ 6


struct wasm_exechdr_secinfo *_rtld_exechdr_find_section(struct wash_exechdr_rt *, int, const char *);

// wasmloader

struct reloc_param {
    struct {
        uintptr_t relocbase;
        int32_t initial;
        int32_t maximum;
    } memory;
    struct {
        uintptr_t relocbase;
        int32_t initial;
        int32_t maximum;  
    } indirect_table;
}

struct wasm_import {
    uint16_t wa_modulesz;
    uint16_t wa_namesz;
    const char *wa_module;
    const char *wa_name;
};

struct wasm_loader_meminfo {
    uint32_t min;
    uint32_t max;
    struct wasm_import import;
    uint32_t min_file_offset;
    uint8_t min_lebsz;
    uint8_t max_lebsz;
    uint8_t limit;
    bool shared;
};

struct wasm_loader_tblinfo {
    uint32_t min;
    uint32_t max;
    struct wasm_import import;
    uint32_t min_file_offset;
    uint8_t min_lebsz;
    uint8_t max_lebsz;
    uint8_t limit;
    bool shared;
};

#endif /* _WASM_WASMEXEC_H_*/



#include <sys/stdint.h>
#include <sys/stdbool.h>

// check what private flags are used for segments in rtld.c
#define _RTLD_SEGMENT_ZERO_FILL (1 << 3)

struct rtld_state_common {
    uint32_t ld_mutex;
    uint32_t ld_state;
    uint32_t dsovec_size;
    struct wasm_module_rt **dsovec;
    struct wasm_module_rt *objlist;     // head of the linked list
    struct wasm_module_rt *obj_tail;    // last loaded object in the linked list
    struct wasm_module_rt *main_obj;    // main executable
    struct wasm_module_rt *objself;     // reference to the object/module of the rtld module itself.
    const char *error_message;
    struct _rtld_search_path *ld_paths;
    struct _rtld_search_path *ld_default_paths;
    uint32_t objcount;
    uint32_t objloads;
    bool    ld_trust;	                /* False for setuid and setgid programs */
    // private parts of rtld_state are declared in rtld.c
};
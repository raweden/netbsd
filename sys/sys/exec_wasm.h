

#ifndef _SYS_EXEC_WASM_H_
#define _SYS_EXEC_WASM_H_


struct exec_wasm_hdr {
    unsigned int wa_magic;
    unsigned int wa_version;
    // TODO: add more useful information we might be able to gather..
};


#endif
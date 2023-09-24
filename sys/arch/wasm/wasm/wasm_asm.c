
#include <sys/cdefs.h>
#include <wasm/wasm_inst.h>
#include <wasm/wasm_module.h>

void __panic_abort(void) __WASM_IMPORT(kern, panic_abort);


// this file contains dummy function for whats is translated to a simple instruction in wasm

void
wasm_inst_nop(void)
{
    
}
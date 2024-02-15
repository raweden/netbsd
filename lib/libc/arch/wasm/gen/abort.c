
#include "cdefs.h"
#include <stdlib.h>

#ifndef __WASM_IMPORT
#define __WASM_IMPORT(module, symbol) __attribute__((import_module(#module), import_name(#symbol)))
#endif

void __panic_abort(void) __WASM_IMPORT(kern, panic_abort);

__dead void
abort(void)
{
    __panic_abort();
    exit(1);
}
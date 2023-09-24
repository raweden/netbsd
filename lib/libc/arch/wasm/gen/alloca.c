
#include <stdlib.h>

#ifndef __WASM_IMPORT
#define __WASM_IMPORT(module, symbol) __attribute__((import_module(#module), import_name(#symbol)))
#endif

void *__wasm_alloca(size_t size) __WASM_IMPORT(libc, __wasm_alloca);

void *
alloca(size_t size)
{
    return __wasm_alloca(size);
}
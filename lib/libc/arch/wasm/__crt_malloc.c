

#include <stddef.h>
#include <stdint.h>

#ifndef __WASM_IMPORT
#define __WASM_IMPORT(module, symbol) __attribute__((import_module(#module), import_name(#symbol)))
#endif

void *__crt_malloc(size_t) __WASM_IMPORT(rtld, __crt_malloc);
void *__crt_calloc(size_t num, size_t size) __WASM_IMPORT(rtld, __crt_calloc);
void __crt_free(void *cp) __WASM_IMPORT(rtld, __crt_free);
void *__crt_realloc(void *cp, size_t nbytes) __WASM_IMPORT(rtld, __crt_realloc);


void *
malloc(size_t len)
{
    return __crt_malloc(len);
}

void *
calloc(size_t num, size_t size)
{
    return __crt_calloc(num, size);
}

void
free(void *cp)
{
    return __crt_free(cp);
}

void *
realloc(void *cp, size_t nbytes)
{
    return __crt_realloc(cp, nbytes);
}
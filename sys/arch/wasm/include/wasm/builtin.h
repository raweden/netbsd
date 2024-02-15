
#include <sys/stdint.h>

#ifdef __WASM
#define __WASM_BUILTIN(symbol) __attribute__((import_module("__builtin"), import_name(#symbol)))
#else
__WASM_BUILTIN(x)
#endif





/**
 * translates to `memory.fill` instruction in post-edit (or link-time with ylinker)
 *
 * @param dst The destination address.
 * @param val The value to use as fill
 * @param len The number of bytes to fill.
 */
void wasm_memory_fill(void * dst, int32_t val, uint32_t len) __WASM_BUILTIN(memory_fill);

/**
 * translates to `memory.copy` instruction in post-edit (or link-time with ylinker)
 *
 * @param dst The destination address.
 * @param src The value to use as fill
 * @param len The number of bytes to fill.
 */
void wasm_memory_copy(void * dst, const void *src, uint32_t len) __WASM_BUILTIN(memory_copy);
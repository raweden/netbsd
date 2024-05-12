
#ifndef __WASM_WASM_CRT_H_
#define __WASM_WASM_CRT_H_

#ifndef __WASM_IMPORT
#define __WASM_IMPORT(module, symbol) __attribute__((import_module(#module), import_name(#symbol)))
#endif


void *__crt_mmap(void *, size_t, int, int, int, int64_t, int *) __WASM_IMPORT(rtld, __crt_mmap);
int __crt_munmap(void *, size_t) __WASM_IMPORT(rtld, __crt_munmap);


#endif /* __WASM_WASM_CRT_H_ */

#ifndef __LIBWASM_LOADER_H_
#define __LIBWASM_LOADER_H_

#include <stddef.h>
#include <stdbool.h>

#ifndef DEBUG_DL_LOADING
#define DEBUG_DL_LOADING 0
#endif

#if defined (DEBUG_DL_LOADING) && DEBUG_DL_LOADING
#define dbg_loading(...) printf(__VA_ARGS__)
#else
#define dbg_loading(...)
#endif

bool wasm_loader_has_exechdr_in_buf(const char *buf, size_t len, size_t *dataoff, size_t *datasz);
void rtld_reloc_exechdr(char *buf);


#endif /* __LIBWASM_LOADER_H_ */
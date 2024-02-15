
#ifndef _WASM_BUILTINS_H_
#define _WASM_BUILTINS_H_

#include "wasm_module.h"

/**
 * This method is used to trigger a debug error on JavaScript side.
 */
void __panic_abort(void) __WASM_IMPORT(kern, panic_abort);

#endif /* _WASM_BUILTINS_H_ */
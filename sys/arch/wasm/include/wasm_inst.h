
#ifndef _MACHINE_ATOMIC_INST_H_
#define _MACHINE_ATOMIC_INST_H_

#include <sys/stdint.h>

// TODO: wasm has both signed and unsigned operations.


#if defined(__WASM)
#define __WASM_BUILTIN(symbol) __attribute__((import_module("__builtin"), import_name(#symbol)))
#else
#define __WASM_BUILTIN(x)
#endif

// Memory barrier operations
void membar_acquire(void) __WASM_BUILTIN(atomic_fence);
void membar_release(void) __WASM_BUILTIN(atomic_fence);
void membar_producer(void) __WASM_BUILTIN(atomic_fence);
void membar_consumer(void) __WASM_BUILTIN(atomic_fence);
void membar_sync(void) __WASM_BUILTIN(atomic_fence);

// Deprecated memory barriers
void membar_enter(void) __WASM_BUILTIN(atomic_fence);
void membar_exit(void) __WASM_BUILTIN(atomic_fence);

/**
 * @return Returns the number of woken up agents
 */
uint32_t __builtin_futex_notify(const uint32_t *valp, uint32_t count) __WASM_BUILTIN(memory_atomic_notify);

void atomic_fence(void) __WASM_BUILTIN(atomic_fence);

enum wait_result {
    ATOMIC_WAIT_OK = 0,
    ATOMIC_WAIT_NOT_EQUAL = 1,
    ATOMIC_WAIT_TIMEOUT = 2
};


/**
 * 
 * @see https://github.com/WebAssembly/threads/blob/main/proposals/threads/Overview.md#wait
 * @param timeout specifies a timeout in nanoseconds, timeout < 0 never expires.
 */
enum wait_result __builtin_futex_wait32(volatile uint32_t *valp, uint32_t expected, int64_t timeout) __WASM_BUILTIN(memory_atomic_wait32);

uint8_t __builtin_atomic_load8_u(const uint8_t *valp) __WASM_BUILTIN(i32_atomic_load8_u);
void __builtin_atomic_store8(volatile uint8_t *valp, uint8_t val) __WASM_BUILTIN(i32_atomic_store8);
uint8_t __builtin_atomic_rmw_add8_u(volatile uint8_t *valp, uint8_t val) __WASM_BUILTIN(i32_atomic_rmw8_add_u);
uint8_t __builtin_atomic_rmw_sub8_u(volatile uint8_t *valp, uint8_t val) __WASM_BUILTIN(i32_atomic_rmw8_sub_u);
uint8_t __builtin_atomic_rmw_and8_u(volatile uint8_t *valp, uint8_t val) __WASM_BUILTIN(i32_atomic_rmw8_and_u);
uint8_t __builtin_atomic_rmw_or8_u(volatile uint8_t *valp, uint8_t val) __WASM_BUILTIN(i32_atomic_rmw8_or_u);
uint8_t __builtin_atomic_rmw_xor8_u(volatile uint8_t *valp, uint8_t val) __WASM_BUILTIN(i32_atomic_rmw8_xor_u);
uint8_t __builtin_atomic_rmw_xchg8_u(volatile uint8_t *valp, uint8_t val) __WASM_BUILTIN(i32_atomic_rmw8_xchg_u);
uint8_t __builtin_atomic_rmw_cmpxchg8_u(volatile uint8_t *valp, uint8_t expected, uint8_t replacement) __WASM_BUILTIN(i32_atomic_rmw8_cmpxchg_u);

uint16_t __builtin_atomic_load16_u(const uint16_t *valp) __WASM_BUILTIN(i32_atomic_load16_u);
void __builtin_atomic_store16(volatile uint16_t *valp, uint16_t val) __WASM_BUILTIN(i32_atomic_store16);
uint16_t __builtin_atomic_rmw_add16_u(volatile uint16_t *valp, uint16_t val) __WASM_BUILTIN(i32_atomic_rmw16_add_u);
uint16_t __builtin_atomic_rmw_sub16_u(volatile uint16_t *valp, uint16_t val) __WASM_BUILTIN(i32_atomic_rmw16_sub_u);
uint16_t __builtin_atomic_rmw_and16_u(volatile uint16_t *valp, uint16_t val) __WASM_BUILTIN(i32_atomic_rmw16_and_u);
uint16_t __builtin_atomic_rmw_or16_u(volatile uint16_t *valp, uint16_t val) __WASM_BUILTIN(i32_atomic_rmw16_or_u);
uint16_t __builtin_atomic_rmw_xor16_u(volatile uint16_t *valp, uint16_t val) __WASM_BUILTIN(i32_atomic_rmw16_xor_u);
uint16_t __builtin_atomic_rmw_xchg16_u(volatile uint16_t *valp, uint16_t val) __WASM_BUILTIN(i32_atomic_rmw16_xchg_u);
uint16_t __builtin_atomic_rmw_cmpxchg16_u(volatile uint16_t *valp, uint16_t expected, uint16_t replacement) __WASM_BUILTIN(i32_atomic_rmw16_cmpxchg_u);

uint32_t __builtin_atomic_load32(const uint32_t *valp) __WASM_BUILTIN(i32_atomic_load);
void __builtin_atomic_store32(volatile uint32_t *valp, uint32_t val) __WASM_BUILTIN(i32_atomic_store);
uint32_t __builtin_atomic_rmw_add32(volatile uint32_t *valp, uint32_t val) __WASM_BUILTIN(i32_atomic_rmw_add);
uint32_t __builtin_atomic_rmw_sub32(volatile uint32_t *valp, uint32_t val) __WASM_BUILTIN(i32_atomic_rmw_sub);
uint32_t __builtin_atomic_rmw_and32(volatile uint32_t *valp, uint32_t val) __WASM_BUILTIN(i32_atomic_rmw_and);
uint32_t __builtin_atomic_rmw_or32(volatile uint32_t *valp, uint32_t val) __WASM_BUILTIN(i32_atomic_rmw_or);
uint32_t __builtin_atomic_rmw_xor32(volatile uint32_t *valp, uint32_t val) __WASM_BUILTIN(i32_atomic_rmw_xor);
uint32_t __builtin_atomic_rmw_xchg32(volatile uint32_t *valp, uint32_t val) __WASM_BUILTIN(i32_atomic_rmw_xchg);
uint32_t __builtin_atomic_rmw_cmpxchg32(volatile uint32_t *valp, uint32_t expected, uint32_t replacement) __WASM_BUILTIN(i32_atomic_rmw_cmpxchg);

// 64-bit

/**
 * 
 * @see https://github.com/WebAssembly/threads/blob/main/proposals/threads/Overview.md#wait
 * @param timeout specifies a timeout in nanoseconds, timeout < 0 never expires.
 */
enum wait_result __builtin_futex_wait64(volatile uint64_t *valp, uint64_t expected, int64_t timeout) __WASM_BUILTIN(memory_atomic_wait64);

uint64_t __builtin_atomic_load64(const uint64_t *valp) __WASM_BUILTIN(i64_atomic_load);
void __builtin_atomic_store64(volatile uint64_t *valp, uint64_t val) __WASM_BUILTIN(i64_atomic_store);
uint64_t __builtin_atomic_rmw_add64(volatile uint64_t *valp, uint64_t val) __WASM_BUILTIN(i64_atomic_rmw_add);
uint64_t __builtin_atomic_rmw_sub64(volatile uint64_t *valp, uint64_t val) __WASM_BUILTIN(i64_atomic_rmw_sub);
uint64_t __builtin_atomic_rmw_and64(volatile uint64_t *valp, uint64_t val) __WASM_BUILTIN(i64_atomic_rmw_and);
uint64_t __builtin_atomic_rmw_or64(volatile uint64_t *valp, uint64_t val) __WASM_BUILTIN(i64_atomic_rmw_or);
uint64_t __builtin_atomic_rmw_xor64(volatile uint64_t *valp, uint64_t val) __WASM_BUILTIN(i64_atomic_rmw_xor);
uint64_t __builtin_atomic_rmw_xchg64(volatile uint64_t *valp, uint64_t val) __WASM_BUILTIN(i64_atomic_rmw_xchg);
uint64_t __builtin_atomic_rmw_cmpxchg64(volatile uint64_t *valp, uint64_t expected, uint64_t replacement) __WASM_BUILTIN(i64_atomic_rmw_cmpxchg);

void wasm_inst_nop(void);


#endif /* _MACHINE_ATOMIC_INST_H_ */
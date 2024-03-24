
#ifndef _WASM_LIBWASM_H_
#define _WASM_LIBWASM_H_

#include <sys/stdint.h> // uinX_t

#define WASM_HDR_SIGN_LE        0x6D736100     // \x00asm

#define WASM_SECTION_TYPE       0x01
#define WASM_SECTION_IMPORT     0x02
#define WASM_SECTION_FUNC       0x03
#define WASM_SECTION_TABLE      0x04
#define WASM_SECTION_MEMORY     0x05
#define WASM_SECTION_GLOBAL     0x06
#define WASM_SECTION_EXPORT     0x07
#define WASM_SECTION_START      0x08
#define WASM_SECTION_ELEMENT    0x09
#define WASM_SECTION_CODE       0x0A
#define WASM_SECTION_DATA       0x0B
#define WASM_SECTION_DATA_COUNT 0x0C
#define WASM_SECTION_TAG        0x0D
#define WASM_SECTION_CUSTOM     0x00

#define WASM_TYPE_I32 0x7F
#define WASM_TYPE_I64 0x7E
#define WASM_TYPE_F32 0x7D
#define WASM_TYPE_F64 0x7C
#define WASM_TYPE_VOID 0x00
#define WASM_TYPE_V128 0x7b
#define WASM_TYPE_FUNCREF 0x70
#define WASM_TYPE_EXTERNREF 0x67

// linking/reloc produced by clang/llvm
#define R_WASM_TABLE_INDEX_SLEB 0x01
#define R_WASM_TABLE_INDEX_I32  0x02
#define R_WASM_MEMORY_ADDR_LEB  0x03
#define R_WASM_MEMORY_ADDR_SLEB 0x04
#define R_WASM_MEMORY_ADDR_I32  0x05


unsigned encodeSLEB128(int64_t, uint8_t *, unsigned);
unsigned encodeULEB128(uint64_t, uint8_t *, unsigned);
uint64_t decodeULEB128(const uint8_t *, unsigned *, const uint8_t *, const char **);
int64_t decodeSLEB128(const uint8_t *, unsigned *, const uint8_t *, const char **);

unsigned getULEB128Size(uint64_t);
unsigned getSLEB128Size(int64_t);

/**
 * When argc/retc have a value of 4 or below, the space that would otherwise hold a pointer is
 * used to store the types itselfs.
 */
struct wasm_type {
    uint16_t argc;
    uint16_t retc;
    union {
        const uint8_t *ptr;
        uint8_t types[4];
    } argv;
    union {
        const uint8_t *ptr;
        uint8_t types[4];
    } retv;
};

#endif /* _WASM_LIBWASM_H_ */
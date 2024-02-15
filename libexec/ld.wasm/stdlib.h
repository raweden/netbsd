#ifndef __RTLD_WASM_STDLIB_H_
#define __RTLD_WASM_STDLIB_H_

#include <sys/errno.h>
#include <sys/stdint.h>
#include <sys/stdbool.h>

#include <dlfcn.h>

#include "rtsys.h"

// stdlib
extern int errno;
int strncmp(const char *s1, const char *s2, unsigned int n);
int strnlen(const char *s1, unsigned int n);
int strlen(const char *str);
char *strchr(const char *s, int c);
int strcmp(const char *s1, const char *s2);
char *strrchr(char const *s, int c);
void *memchr(const void *s, int c, size_t n);
size_t strlcpy(char *dst, const char *src, size_t siz);


// stdarg.h
typedef __builtin_va_list __va_list;
#ifndef __VA_LIST_DECLARED
typedef __va_list va_list;
#define __VA_LIST_DECLARED
#endif

#define	va_start(ap, last)	__builtin_va_start((ap), (last))
#define	va_arg			__builtin_va_arg
#define	va_end(ap)		__builtin_va_end(ap)
#define	__va_copy(dest, src)	__builtin_va_copy((dest), (src))

// printf.h
int vsnprintf(char *bf, size_t size, const char *fmt, va_list ap);

struct wasm_module_rt;

int _rtld_find_dso_library(const char *filepath, const char *pathbuf, uint32_t *pathsz);
int _rtld_load_dso_library(const char *filepath, struct wasm_module_rt **module);


#endif /* __RTLD_WASM_STDLIB_H_ */
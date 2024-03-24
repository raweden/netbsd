/*
 * Copyright (c) 2024 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Raweden @github 2024.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "stdlib.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

int
strncmp(const char *s1, const char *s2, unsigned int n)
{
    if (n == 0)
		return (0);
	do {
		if (*s1 != *s2++)
			return (*(const unsigned char *)s1 -
				*(const unsigned char *)(s2 - 1));
		if (*s1++ == '\0')
			break;
	} while (--n != 0);

	return (0);
}

int
strnlen(const char *s, unsigned int maxlen)
{
	int len;

	for (len = 0; len < maxlen; len++, s++) {
		if (!*s)
			break;
	}
	return (len);
}

/*
 * Compare strings.
 */
int
strcmp(const char *s1, const char *s2)
{

	while (*s1 == *s2++)
		if (*s1++ == 0)
			return (0);
	return (*(const unsigned char *)s1 - *(const unsigned char *)--s2);
}

int
strlen(const char *str)
{
	const char *s;

	for (s = str; *s; ++s)
		continue;
	return(s - str);
}


char *
strchr(const char *s, int c)
{
    do {
        if ((unsigned char)*s == (unsigned char)c)
            return (char *)s;

    } while (*(++s) != 0);

    return NULL;
}

char *
strrchr(char const *s, int c)
{
    char const *e = s + strlen(s);

    for (;;) {
        if (--e < s)
            break;

        if ((unsigned char)*e == (unsigned char)c)
            return (char *)e;
    }
    return NULL;
}

/*
 * Copy src to string dst of size siz.  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz == 0).
 * Returns strlen(src); if retval >= siz, truncation occurred.
 */
size_t
strlcpy(char *dst, const char *src, size_t siz)
{
	char *d = dst;
	const char *s = src;
	size_t n = siz;

	/* Copy as many bytes as will fit */
	if (n != 0 && --n != 0) {
		do {
			if ((*d++ = *s++) == 0)
				break;
		} while (--n != 0);
	}

	/* Not enough room in dst, add NUL and traverse rest of src */
	if (n == 0) {
		if (siz != 0)
			*d = '\0';		/* NUL-terminate dst */
		while (*s++)
			;
	}

	return(s - src - 1);	/* count does not include NUL */
}

void *
memchr(const void *s, int c, size_t n)
{

	if (n != 0) {
		const unsigned char *p = s;
		const unsigned char cmp = c;

		do {
			if (*p++ == cmp)
				return __UNCONST(p - 1);
		} while (--n != 0);
	}
	return NULL;
}

/*
 * Find the first occurrence of find in s.
 */
char *
strstr(const char *s, const char *find)
{
	char c, sc;
	size_t len;

	if ((c = *find++) != 0) {
		len = strlen(find);
		do {
			do {
				if ((sc = *s++) == 0)
					return (NULL);
			} while (sc != c);
		} while (strncmp(s, find, len) != 0);
		s--;
	}
	return __UNCONST(s);
}

int
sprintf(char *buf, int bufsz, const char *fmt, ...)
{
	int result;
	va_list ap;

	va_start(ap, fmt);
	result = vsnprintf(buf, bufsz, fmt, ap);
	va_end(ap);

	return result;
}

/*
 * Copy src to dst, truncating or null-padding to always copy n bytes.
 * Return dst.
 */
char *
strncpy(char *dst, const char *src, size_t n)
{

	if (n != 0) {
		char *d = dst;
		const char *s = src;

		do {
			if ((*d++ = *s++) == 0) {
				/* NUL pad the remaining n-1 bytes */
				while (--n != 0)
					*d++ = 0;
				break;
			}
		} while (--n != 0);
	}
	return (dst);
}

/*
 * Compare memory regions.
 */
int
memcmp(const void *s1, const void *s2, size_t n)
{
	const unsigned char *c1, *c2;

#ifndef _STANDALONE
	const uintptr_t *b1, *b2;

	b1 = s1;
	b2 = s2;

#ifndef __NO_STRICT_ALIGNMENT
	if ((((uintptr_t)b1 | (uintptr_t)b2) & (sizeof(uintptr_t) - 1)) == 0)
#endif
	{
		while (n >= sizeof(uintptr_t)) {
			if (*b1 != *b2)
				break;
			b1++;
			b2++;
			n -= sizeof(uintptr_t);
		}
	}

	c1 = (const unsigned char *)b1;
	c2 = (const unsigned char *)b2;
#else
	c1 = (const unsigned char *)s1;
	c2 = (const unsigned char *)s2;
#endif

	if (n != 0) {
		do {
			if (*c1++ != *c2++)
				return *--c1 - *--c2;
		} while (--n != 0);
	}

	return 0;
}
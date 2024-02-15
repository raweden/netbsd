
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
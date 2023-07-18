
#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

#include "defs.h"

# include <assert.h>
# include <errno.h>
# include <stdio.h>
# include <stdlib.h>
# include <unistd.h>
# include <fcntl.h>

#include "/home/raweden/Projects/netbsd-src/include/vis.h"

# define	GETTEMP		__gettemp

#define	O_SHLOCK	0x00000010	/* open with shared file lock */
#define	O_EXLOCK	0x00000020	/* open with exclusive file lock */
#define	O_ASYNC		0x00000040	/* signal pgrp when data ready */
#define	O_DIRECT	0x00080000	/* direct I/O hint */

#if 0
static int
fmtassert(char *buf, size_t len, const char *file, int line,
    const char *function, const char *failedexpr)
{
	return snprintf_ss(buf, len,
	    "assertion \"%s\" failed: file \"%s\", line %d%s%s%s\n",
	    failedexpr, file, line,
	    function ? ", function \"" : "",
	    function ? function : "",
	    function ? "\"" : "");
}

void
__assert13(const char *file, int line, const char *function,
    const char *failedexpr)
{
	char buf[1024];
	int l = fmtassert(buf, sizeof(buf), file, line, function, failedexpr);
	if (l < 0)
		abort();
	(void)write(STDERR_FILENO, buf, (size_t)l);
	abort();
	/* NOTREACHED */
}
#endif

#if 0
struct stat {
    dev_t     st_dev;     /* ID of device containing file */
    ino_t     st_ino;     /* inode number */
    mode_t    st_mode;    /* protection */
    nlink_t   st_nlink;   /* number of hard links */
    uid_t     st_uid;     /* user ID of owner */
    gid_t     st_gid;     /* group ID of owner */
    dev_t     st_rdev;    /* device ID (if special file) */
    off_t     st_size;    /* total size, in bytes */
    blksize_t st_blksize; /* blocksize for file system I/O */
    blkcnt_t  st_blocks;  /* number of 512B blocks allocated */
    time_t    st_atime;   /* time of last access */
    time_t    st_mtime;   /* time of last modification */
    time_t    st_ctime;   /* time of last status change */
};
#endif

#if 0
#define isoctal(c)	(((u_char)(c)) >= '0' && ((u_char)(c)) <= '7')
#define iswhite(c)	(c == ' ' || c == '\t' || c == '\n')
#define issafe(c)	(c == '\b' || c == BELL || c == '\r')
#define xtoa(c)		"0123456789abcdef"[c]

#define MAXEXTRAS	5

/*
 * Expand list of extra characters to not visually encode.
 */
static wchar_t *
makeextralist(int flags, const char *src)
{
	wchar_t *dst, *d;
	size_t len;
	const wchar_t *s;
	mbstate_t mbstate;

	len = strlen(src);
	if ((dst = calloc(len + MAXEXTRAS, sizeof(*dst))) == NULL)
		return NULL;

	memset(&mbstate, 0, sizeof(mbstate));
	if ((flags & VIS_NOLOCALE)
	    || mbsrtowcs(dst, &src, len, &mbstate) == (size_t)-1) {
		size_t i;
		for (i = 0; i < len; i++)
			dst[i] = (wchar_t)(u_char)src[i];
		d = dst + len;
	} else
		d = dst + wcslen(dst);

	if (flags & VIS_GLOB)
		for (s = char_glob; *s; *d++ = *s++)
			continue;

	if (flags & VIS_SHELL)
		for (s = char_shell; *s; *d++ = *s++)
			continue;

	if (flags & VIS_SP) *d++ = L' ';
	if (flags & VIS_TAB) *d++ = L'\t';
	if (flags & VIS_NL) *d++ = L'\n';
	if (flags & VIS_DQ) *d++ = L'"';
	if ((flags & VIS_NOSLASH) == 0) *d++ = L'\\';
	*d = L'\0';

	return dst;
}

/*
 * istrsenvisx()
 * 	The main internal function.
 *	All user-visible functions call this one.
 */
static int
istrsenvisx(char **mbdstp, size_t *dlen, const char *mbsrc, size_t mblength,
    int flags, const char *mbextra, int *cerr_ptr)
{
	wchar_t *dst, *src, *pdst, *psrc, *start, *extra;
	size_t len, olen;
	uint64_t bmsk, wmsk;
	wint_t c;
	visfun_t f;
	int clen = 0, cerr, error = -1, i, shft;
	char *mbdst, *mdst;
	ssize_t mbslength, maxolen;
	mbstate_t mbstate;

	mbslength = (ssize_t)mblength;
	/*
	 * When inputing a single character, must also read in the
	 * next character for nextc, the look-ahead character.
	 */
	if (mbslength == 1)
		mbslength++;

	/*
	 * Input (mbsrc) is a char string considered to be multibyte
	 * characters.  The input loop will read this string pulling
	 * one character, possibly multiple bytes, from mbsrc and
	 * converting each to wchar_t in src.
	 *
	 * The vis conversion will be done using the wide char
	 * wchar_t string.
	 *
	 * This will then be converted back to a multibyte string to
	 * return to the caller.
	 */

	/* Allocate space for the wide char strings */
	psrc = pdst = extra = NULL;
	mdst = NULL;
	if ((psrc = calloc(mbslength + 1, sizeof(*psrc))) == NULL)
		return -1;
	if ((pdst = calloc((16 * mbslength) + 1, sizeof(*pdst))) == NULL)
		goto out;
	if (*mbdstp == NULL) {
		if ((mdst = calloc((16 * mbslength) + 1, sizeof(*mdst))) == NULL)
			goto out;
		*mbdstp = mdst;
	}

	mbdst = *mbdstp;
	dst = pdst;
	src = psrc;

	if (flags & VIS_NOLOCALE) {
		/* Do one byte at a time conversion */
		cerr = 1;
	} else {
		/* Use caller's multibyte conversion error flag. */
		cerr = cerr_ptr ? *cerr_ptr : 0;
	}

	/*
	 * Input loop.
	 * Handle up to mblength characters (not bytes).  We do not
	 * stop at NULs because we may be processing a block of data
	 * that includes NULs.
	 */
	memset(&mbstate, 0, sizeof(mbstate));
	while (mbslength > 0) {
		/* Convert one multibyte character to wchar_t. */
		if (!cerr)
			clen = mbrtowc(src, mbsrc, MIN(mbslength, MB_LEN_MAX),
			    &mbstate);
		if (cerr || clen < 0) {
			/* Conversion error, process as a byte instead. */
			*src = (wint_t)(u_char)*mbsrc;
			clen = 1;
			cerr = 1;
		}
		if (clen == 0) {
			/*
			 * NUL in input gives 0 return value. process
			 * as single NUL byte and keep going.
			 */
			clen = 1;
		}
		/* Advance buffer character pointer. */
		src++;
		/* Advance input pointer by number of bytes read. */
		mbsrc += clen;
		/* Decrement input byte count. */
		mbslength -= clen;
	}
	len = src - psrc;
	src = psrc;

	/*
	 * In the single character input case, we will have actually
	 * processed two characters, c and nextc.  Reset len back to
	 * just a single character.
	 */
	if (mblength < len)
		len = mblength;

	/* Convert extra argument to list of characters for this mode. */
	extra = makeextralist(flags, mbextra);
	if (!extra) {
		if (dlen && *dlen == 0) {
			errno = ENOSPC;
			goto out;
		}
		*mbdst = '\0';	/* can't create extra, return "" */
		error = 0;
		goto out;
	}

	/* Look up which processing function to call. */
	f = getvisfun(flags);

	/*
	 * Main processing loop.
	 * Call do_Xvis processing function one character at a time
	 * with next character available for look-ahead.
	 */
	for (start = dst; len > 0; len--) {
		c = *src++;
		dst = (*f)(dst, c, flags, len >= 1 ? *src : L'\0', extra);
		if (dst == NULL) {
			errno = ENOSPC;
			goto out;
		}
	}

	/* Terminate the string in the buffer. */
	*dst = L'\0';

	/*
	 * Output loop.
	 * Convert wchar_t string back to multibyte output string.
	 * If we have hit a multi-byte conversion error on input,
	 * output byte-by-byte here.  Else use wctomb().
	 */
	len = wcslen(start);
	maxolen = dlen ? *dlen : (wcslen(start) * MB_LEN_MAX + 1);
	olen = 0;
	memset(&mbstate, 0, sizeof(mbstate));
	for (dst = start; len > 0; len--) {
		if (!cerr)
			clen = wcrtomb(mbdst, *dst, &mbstate);
		if (cerr || clen < 0) {
			/*
			 * Conversion error, process as a byte(s) instead.
			 * Examine each byte and higher-order bytes for
			 * data.  E.g.,
			 *	0x000000000000a264 -> a2 64
			 *	0x000000001f00a264 -> 1f 00 a2 64
			 */
			clen = 0;
			wmsk = 0;
			for (i = sizeof(wmsk) - 1; i >= 0; i--) {
				shft = i * NBBY;
				bmsk = (uint64_t)0xffLL << shft;
				wmsk |= bmsk;
				if ((*dst & wmsk) || i == 0)
					mbdst[clen++] = (char)(
					    (uint64_t)(*dst & bmsk) >>
					    shft);
			}
			cerr = 1;
		}
		/* If this character would exceed our output limit, stop. */
		if (olen + clen > (size_t)maxolen)
			break;
		/* Advance output pointer by number of bytes written. */
		mbdst += clen;
		/* Advance buffer character pointer. */
		dst++;
		/* Incrment output character count. */
		olen += clen;
	}

	/* Terminate the output string. */
	*mbdst = '\0';

	if (flags & VIS_NOLOCALE) {
		/* Pass conversion error flag out. */
		if (cerr_ptr)
			*cerr_ptr = cerr;
	}

	free(extra);
	free(pdst);
	free(psrc);

	return (int)olen;
out:
	free(extra);
	free(pdst);
	free(psrc);
	free(mdst);
	return error;
}

static int
istrsenvisxl(char **mbdstp, size_t *dlen, const char *mbsrc,
    int flags, const char *mbextra, int *cerr_ptr)
{
	return istrsenvisx(mbdstp, dlen, mbsrc,
	    mbsrc != NULL ? strlen(mbsrc) : 0, flags, mbextra, cerr_ptr);
}
#endif

int
strvis(char *mbdst, const char *mbsrc, int flags)
{
	fprintf(stderr, "called %s",__func__);
    exit(EXIT_FAILURE);
}

int     GETTEMP(char *, int *, int, int, int);

static const unsigned char padchar[] =
"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

int
GETTEMP(char *path, int *doopen, int domkdir, int slen, int oflags)
{
	char *start, *trv, *suffp, *carryp;
	char *pad;
	struct stat sbuf;
	int rval;
	uint32_t r;
	char carrybuf[MAXPATHLEN];

	/* doopen may be NULL */
	if ((doopen != NULL && domkdir) || slen < 0 ||
	    (oflags & ~(O_APPEND | O_DIRECT | O_SHLOCK | O_EXLOCK | O_SYNC |
	    O_CLOEXEC)) != 0) {
		errno = EINVAL;
		return 0;
	}

	for (trv = path; *trv != '\0'; ++trv)
		continue;

	if (trv - path >= MAXPATHLEN) {
		errno = ENAMETOOLONG;
		return 0;
	}
	trv -= slen;
	suffp = trv;
	--trv;
	if (trv < path || NULL != strchr(suffp, '/')) {
		errno = EINVAL;
		return 0;
	}

	/* Fill space with random characters */
	while (trv >= path && *trv == 'X') {
		r = arc4random_uniform((unsigned int)(sizeof(padchar) - 1));
		*trv-- = padchar[r];
	}
	start = trv + 1;

	/* save first combination of random characters */
	memcpy(carrybuf, start, (size_t)(suffp - start));

	/*
	 * check the target directory.
	 */
	if (doopen != NULL || domkdir) {
		for (; trv > path; --trv) {
			if (*trv == '/') {
				*trv = '\0';
				rval = stat(path, &sbuf);
				*trv = '/';
				if (rval != 0)
					return 0;
				if (!S_ISDIR(sbuf.st_mode)) {
					errno = ENOTDIR;
					return 0;
				}
				break;
			}
		}
	}

	for (;;) {
		if (doopen) {
			if ((*doopen = open(path, O_CREAT|O_EXCL|O_RDWR|oflags,
			    0600)) != -1)
				return 1;
			if (errno != EEXIST)
				return 0;
		} else if (domkdir) {
			if (mkdir(path, 0700) != -1)
				return 1;
			if (errno != EEXIST)
				return 0;
		} else if (lstat(path, &sbuf))
			return errno == ENOENT;

		/*
		 * If we have a collision,
		 * cycle through the space of filenames
		 */
		for (trv = start, carryp = carrybuf;;) {
			/* have we tried all possible permutations? */
			if (trv == suffp)
				return 0; /* yes - exit with EEXIST */
			pad = strchr((const char *)padchar, *trv);
			if (pad == NULL) {
				/* this should never happen */
				errno = EIO;
				return 0;
			}
			/* increment character */
			*trv = (*++pad == '\0') ? padchar[0] : *pad;
			/* carry to next position? */
			if (*trv == *carryp) {
				/* increment position and loop */
				++trv;
				++carryp;
			} else {
				/* try with new name */
				break;
			}
		}
	}
	/*NOTREACHED*/
}

int
mkstemp(char *path)
{
	int fd;

	return GETTEMP(path, &fd, 0, 0, 0) ? fd : -1;
}

void
vwarnc(int code, const char *fmt, va_list ap)
{
	(void)fprintf(stderr, "%s: ", getprogname());
	if (fmt != NULL) {
		(void)vfprintf(stderr, fmt, ap);
		(void)fprintf(stderr, ": ");
	}
	(void)fprintf(stderr, "%s\n", strerror(code));
}

void
vwarn(const char *fmt, va_list ap)
{
	vwarnc(errno, fmt, ap);
}

void
warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarn(fmt, ap);
	va_end(ap);
}

void
warnx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vwarnx(fmt, ap);
	va_end(ap);
}

void
vwarnx(const char *fmt, va_list ap)
{
	(void)fprintf(stderr, "%s: ", getprogname());
	if (fmt != NULL)
		(void)vfprintf(stderr, fmt, ap);
	(void)fprintf(stderr, "\n");
}

void
verrc(int eval, int code, const char *fmt, va_list ap)
{
	(void)fprintf(stderr, "%s: ", getprogname());
	if (fmt != NULL) {
		(void)vfprintf(stderr, fmt, ap);
		(void)fprintf(stderr, ": ");
	}
	(void)fprintf(stderr, "%s\n", strerror(code));
	exit(eval);
}

void
verr(int eval, const char *fmt, va_list ap)
{
	verrc(eval, errno, fmt, ap);
}

void
err(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verr(eval, fmt, ap);
	va_end(ap);
}

void
verrx(int eval, const char *fmt, va_list ap)
{
	(void)fprintf(stderr, "%s: ", getprogname());
	if (fmt != NULL)
		(void)vfprintf(stderr, fmt, ap);
	(void)fprintf(stderr, "\n");
	exit(eval);
}

void
errx(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verrx(eval, fmt, ap);
	va_end(ap);
}


/*
 * char *realpath(const char *path, char *resolved);
 *
 * Find the real name of path, by removing all ".", ".." and symlink
 * components.  Returns (resolved) on success, or (NULL) on failure,
 * in which case the path which caused trouble is left in (resolved).
 */
char *
realpath(const char * __restrict path, char * __restrict resolved)
{
	struct stat sb;
	int idx = 0, nlnk = 0;
	const char *q;
	char *p, wbuf[2][MAXPATHLEN], *fres;
	size_t len;
	ssize_t n;

	/* POSIX sez we must test for this */
	if (path == NULL) {
		errno = EINVAL;
		return NULL;
	}

	if (resolved == NULL) {
		fres = resolved = malloc(MAXPATHLEN);
		if (resolved == NULL)
			return NULL;
	} else
		fres = NULL;


	/*
	 * Build real path one by one with paying an attention to .,
	 * .. and symbolic link.
	 */

	/*
	 * `p' is where we'll put a new component with prepending
	 * a delimiter.
	 */
	p = resolved;

	if (*path == '\0') {
		*p = '\0';
		errno = ENOENT;
		goto out;
	}

	/* If relative path, start from current working directory. */
	if (*path != '/') {
		/* check for resolved pointer to appease coverity */
		if (resolved && getcwd(resolved, MAXPATHLEN) == NULL) {
			p[0] = '.';
			p[1] = '\0';
			goto out;
		}
		len = strlen(resolved);
		if (len > 1)
			p += len;
	}

loop:
	/* Skip any slash. */
	while (*path == '/')
		path++;

	if (*path == '\0') {
		if (p == resolved)
			*p++ = '/';
		*p = '\0';
		return resolved;
	}

	/* Find the end of this component. */
	q = path;
	do
		q++;
	while (*q != '/' && *q != '\0');

	/* Test . or .. */
	if (path[0] == '.') {
		if (q - path == 1) {
			path = q;
			goto loop;
		}
		if (path[1] == '.' && q - path == 2) {
			/* Trim the last component. */
			if (p != resolved)
				while (*--p != '/')
					continue;
			path = q;
			goto loop;
		}
	}

	/* Append this component. */
	if (p - resolved + 1 + q - path + 1 > MAXPATHLEN) {
		errno = ENAMETOOLONG;
		if (p == resolved)
			*p++ = '/';
		*p = '\0';
		goto out;
	}
	p[0] = '/';
	memcpy(&p[1], path,
	    /* LINTED We know q > path. */
	    q - path);
	p[1 + q - path] = '\0';

	/*
	 * If this component is a symlink, toss it and prepend link
	 * target to unresolved path.
	 */
	if (lstat(resolved, &sb) == -1)
		goto out;

	if (S_ISLNK(sb.st_mode)) {
		if (nlnk++ >= MAXSYMLINKS) {
			errno = ELOOP;
			goto out;
		}
		n = readlink(resolved, wbuf[idx], sizeof(wbuf[0]) - 1);
		if (n < 0)
			goto out;
		if (n == 0) {
			errno = ENOENT;
			goto out;
		}

		/* Append unresolved path to link target and switch to it. */
		if (n + (len = strlen(q)) + 1 > sizeof(wbuf[0])) {
			errno = ENAMETOOLONG;
			goto out;
		}
		memcpy(&wbuf[idx][n], q, len + 1);
		path = wbuf[idx];
		idx ^= 1;

		/* If absolute symlink, start from root. */
		if (*path == '/')
			p = resolved;
		goto loop;
	}
	if (*q == '/' && !S_ISDIR(sb.st_mode)) {
		errno = ENOTDIR;
		goto out;
	}

	/* Advance both resolved and unresolved path. */
	p += 1 + q - path;
	path = q;
	goto loop;
out:
	free(fres);
	return NULL;
}

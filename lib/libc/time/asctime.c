/*	$NetBSD: asctime.c,v 1.28 2022/08/16 10:56:21 christos Exp $	*/

/* asctime and asctime_r a la POSIX and ISO C, except pad years before 1000.  */

/*
** This file is in the public domain, so clarified as of
** 1996-06-05 by Arthur David Olson.
*/

/*
** Avoid the temptation to punt entirely to strftime;
** the output of strftime is supposed to be locale specific
** whereas the output of asctime is supposed to be constant.
*/

#include <sys/cdefs.h>
#if defined(LIBC_SCCS) && !defined(lint)
#if 0
static char	elsieid[] = "@(#)asctime.c	8.5";
#else
__RCSID("$NetBSD: asctime.c,v 1.28 2022/08/16 10:56:21 christos Exp $");
#endif
#endif /* LIBC_SCCS and not lint */

/*LINTLIBRARY*/

#include "namespace.h"
#include "private.h"
#include <stdio.h>

#if defined(__weak_alias) && !defined(__WASM)
__weak_alias(asctime_r,_asctime_r)
#endif

/*
** All years associated with 32-bit time_t values are exactly four digits long;
** some years associated with 64-bit time_t values are not.
** Vintage programs are coded for years that are always four digits long
** and may assume that the newline always lands in the same place.
** For years that are less than four digits, we pad the output with
** leading zeroes to get the newline in the traditional place.
** The -4 ensures that we get four characters of output even if
** we call a strftime variant that produces fewer characters for some years.
** The ISO C and POSIX standards prohibit padding the year,
** but many implementations pad anyway; most likely the standards are buggy.
*/
static char const ASCTIME_FMT[] = "%s %s%3d %.2d:%.2d:%.2d %-4s\n";
/*
** For years that are more than four digits we put extra spaces before the year
** so that code trying to overwrite the newline won't end up overwriting
** a digit within a year and truncating the year (operating on the assumption
** that no output is better than wrong output).
*/
static char const ASCTIME_FMT_B[] = "%s %s%3d %.2d:%.2d:%.2d     %s\n";

enum { STD_ASCTIME_BUF_SIZE = 26 };
/*
** Big enough for something such as
** ??? ???-2147483648 -2147483648:-2147483648:-2147483648     -2147483648\n
** (two three-character abbreviations, five strings denoting integers,
** seven explicit spaces, two explicit colons, a newline,
** and a trailing NUL byte).
** The values above are for systems where an int is 32 bits and are provided
** as an example; the size expression below is a bound for the system at
** hand.
*/
static char buf_asctime[2*3 + 5*INT_STRLEN_MAXIMUM(int) + 7 + 2 + 1 + 1];

char *
asctime_r(const struct tm *timeptr, char *buf)
{
	static const char	wday_name[][4] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};
	static const char	mon_name[][4] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	const char *	wn;
	const char *	mn;
	char			year[INT_STRLEN_MAXIMUM(int) + 2];
	char result[sizeof buf_asctime];

	if (timeptr == NULL) {
		errno = EINVAL;
		return strcpy(buf, "??? ??? ?? ??:??:?? ????\n");
	}
	if (timeptr->tm_wday < 0 || timeptr->tm_wday >= DAYSPERWEEK)
		wn = "???";
	else	wn = wday_name[timeptr->tm_wday];
	if (timeptr->tm_mon < 0 || timeptr->tm_mon >= MONSPERYEAR)
		mn = "???";
	else	mn = mon_name[timeptr->tm_mon];
	/*
	** Use strftime's %Y to generate the year, to avoid overflow problems
	** when computing timeptr->tm_year + TM_YEAR_BASE.
	** Assume that strftime is unaffected by other out-of-range members
	** (e.g., timeptr->tm_mday) when processing "%Y".
	*/
	(void) strftime(year, sizeof year, "%Y", timeptr);
	(void) snprintf(result,
		sizeof(result),
		((strlen(year) <= 4) ? ASCTIME_FMT : ASCTIME_FMT_B),
		wn, mn,
		timeptr->tm_mday, timeptr->tm_hour,
		timeptr->tm_min, timeptr->tm_sec,
		year);
	if (strlen(result) < STD_ASCTIME_BUF_SIZE || buf == buf_asctime)
		return strcpy(buf, result);
	else {
		errno = EOVERFLOW;
		return NULL;
	}
}

char *
asctime(const struct tm *timeptr)
{
	return asctime_r(timeptr, buf_asctime);
}

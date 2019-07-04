/*
 * Copyright (c) 2016, DDN Storage Corporation.
 */
/*
 *
 * Debug library.
 *
 * Author: Li Xi <lixi@ddn.com>
 */

#ifndef _LCRP_DEBUG_H_
#define _LCRP_DEBUG_H_

#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <stdbool.h>

enum {
	ERROR	= 0,
	INFO,
	DEBUG,
};

extern int debug_level;
extern FILE *debug_log;
extern FILE *info_log;
extern FILE *error_log;

/* Print debug information. This is controlled by the value of the
 * global variable 'debug'
 */
static inline void _lcrp_logging(int level, bool watch, const char *fmt, ...)
{
	va_list ap;

	if (level > debug_level && !watch)
		return;

	if (debug_log != NULL) {
		va_start(ap, fmt);
		vfprintf(debug_log, fmt, ap);
		va_end(ap);
		fflush(debug_log);
	}

	if (info_log != NULL && level <= INFO) {
		va_start(ap, fmt);
		vfprintf(info_log, fmt, ap);
		va_end(ap);
		fflush(info_log);
	}

	if (error_log != NULL && level <= ERROR) {
		va_start(ap, fmt);
		vfprintf(error_log, fmt, ap);
		va_end(ap);
		fflush(error_log);
	}

	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fflush(stdout);
}

#define lcrp_logging(level, watch, format, args...) \
	_lcrp_logging(level, watch, "["#level"] [%s:%d] [%s()]: " format, \
		      __FILE__,  __LINE__, __func__, ##args)


#define LERROR(format, args...) \
	lcrp_logging(ERROR, false, format, ##args)

#define LDEBUG(format, args...) \
	lcrp_logging(DEBUG, false, format, ##args)

#define LINFO(format, args...) \
	lcrp_logging(INFO, false, format, ##args)

#define _LINFO(format, args...) \
	_lcrp_logging(INFO, false, format, ##args)

#define LERRORW(watch, format, args...) \
	lcrp_logging(ERROR, watch, format, ##args)

#define LDEBUGW(watch, format, args...) \
	lcrp_logging(DEBUG, watch, format, ##args)

#define LINFOW(watch, format, args...) \
	lcrp_logging(INFO, watch, format, ##args)

typedef long long_ptr_t;

#define LRETURN(rc)							\
do {									\
	typeof(rc) __rc = (rc);						\
	LDEBUG("Process leaving (rc=%lu : %ld : %lx)\n",		\
	       (long)__rc, (long)__rc, (long)__rc);			\
	return __rc;							\
} while (0)
#define LENTRY	LDEBUG("Process entered\n")
#define LEXIT	LDEBUG("Process leaving\n")

#define LCRP_ALLOC(ptr, size)	((ptr) = malloc(size))
#define LCRP_ALLOC_PTR(ptr) LCRP_ALLOC(ptr, sizeof *(ptr))

#define LCRP_FREE(ptr, size)	free(ptr)
#define LCRP_FREE_PTR(ptr) LCRP_FREE(ptr, sizeof *(ptr))

#define GOTO(label, rc)							\
do {									\
	typeof(rc) __rc = (rc);						\
	LDEBUG("Process leaving via %s (rc=%lu : %ld : %lx)\n",		\
	       #label, (long)__rc, (long)__rc, (long)__rc);		\
	goto label;							\
} while (0)

#define LBUG() assert(0)
#define LASSERT(e) assert(e)

#ifndef offsetof
# define offsetof(typ, memb) ((unsigned long)((char *)&(((typ *)0)->memb)))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) ((sizeof(a)) / (sizeof((a)[0])))
#endif

#endif /* _LCRP_DEBUG_H_ */

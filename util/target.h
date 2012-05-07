#ifndef _PERF_TARGET_H
#define _PERF_TARGET_H

#include <stdbool.h>
#include <sys/types.h>

struct perf_target {
	const char   *pid;
	const char   *tid;
	const char   *cpu_list;
	const char   *uid_str;
	uid_t	     uid;
	bool	     system_wide;
};

enum perf_target_errno {
	PERF_ERRNO_TARGET__SUCCESS		= 0,

	/*
	 * Choose an arbitrary negative big number not to clash with standard
	 * errno since SUS requires the errno has distinct positive values.
	 * See 'Issue 6' in the link below.
	 *
	 * http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/errno.h.html
	 */
	__PERF_ERRNO_TARGET__START		= -10000,


	/* for perf_target__validate() */
	PERF_ERRNO_TARGET__PID_OVERRIDE_CPU	= __PERF_ERRNO_TARGET__START,
	PERF_ERRNO_TARGET__PID_OVERRIDE_UID,
	PERF_ERRNO_TARGET__UID_OVERRIDE_CPU,
	PERF_ERRNO_TARGET__PID_OVERRIDE_SYSTEM,
	PERF_ERRNO_TARGET__UID_OVERRIDE_SYSTEM,

	__PERF_ERRNO_TARGET__END,
};

enum perf_target_errno perf_target__validate(struct perf_target *target);

#endif /* _PERF_TARGET_H */

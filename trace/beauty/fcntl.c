/*
 * trace/beauty/fcntl.c
 *
 *  Copyright (C) 2017, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 *
 * Released under the GPL v2. (and only v2, not any later version)
 */

#include "trace/beauty/beauty.h"
#include <uapi/linux/fcntl.h>

size_t syscall_arg__scnprintf_fcntl_cmd(char *bf, size_t size, struct syscall_arg *arg)
{
	/*
	 * Some commands ignore the third fcntl argument, "arg", so mask it
	 */
	if (arg->val == F_GETFD	   || arg->val == F_GETFL     ||
	    arg->val == F_GETOWN   || arg->val == F_GET_SEALS ||
	    arg->val == F_GETLEASE || arg->val == F_GETSIG)
		arg->mask |= (1 << 2);

	return syscall_arg__scnprintf_strarrays(bf, size, arg);
}

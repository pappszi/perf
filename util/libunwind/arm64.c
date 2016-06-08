/*
 * This file setups defines to compile arch specific binary from the
 * generic one.
 *
 * The function 'LIBUNWIND__ARCH_REG_ID' name is set according to arch
 * name and the defination of this function is included directly from
 * 'arch/arm64/util/unwind-libunwind.c', to make sure that this function
 * is defined no matter what arch the host is.
 *
 * Finally, the arch specific unwind methods are exported which will
 * be assigned to each arm64 thread.
 */

#define REMOTE_UNWIND_LIBUNWIND

#define LIBUNWIND__ARCH_REG_ID(regnum) libunwind__arm64_reg_id(regnum)

#include "unwind.h"
#include "debug.h"
#include "libunwind-aarch64.h"
#include <../../../../arch/arm64/include/uapi/asm/perf_regs.h>
#include "../../arch/arm64/util/unwind-libunwind.c"

/* NO_LIBUNWIND_DEBUG_FRAME is a feature flag for local libunwind,
 * assign NO_LIBUNWIND_DEBUG_FRAME_AARCH64 to it for compiling arm64
 * unwind methods.
 */
#undef NO_LIBUNWIND_DEBUG_FRAME
#ifdef NO_LIBUNWIND_DEBUG_FRAME_AARCH64
#define NO_LIBUNWIND_DEBUG_FRAME
#endif
#include "util/unwind-libunwind-local.c"

struct unwind_libunwind_ops *
arm64_unwind_libunwind_ops = &_unwind_libunwind_ops;

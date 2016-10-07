/*
 * intel_pt_insn_decoder.c: Intel Processor Trace support
 * Copyright (c) 2013-2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <stdio.h>
#include <string.h>
#include <endian.h>
#include <byteswap.h>

#include "event.h"

#include "insn.h"

#include "inat.c"
#include "insn.c"

#include "intel-pt-insn-decoder.h"

#if INTEL_PT_INSN_BUF_SZ < MAX_INSN_SIZE
#error Instruction buffer size too small
#endif

/* Based on branch_type() from perf_event_intel_lbr.c */
static void intel_pt_insn_decoder(struct insn *insn,
				  struct intel_pt_insn *intel_pt_insn)
{
	enum intel_pt_insn_op op = INTEL_PT_OP_OTHER;
	enum intel_pt_insn_branch branch = INTEL_PT_BR_NO_BRANCH;
	int ext;

	if (insn_is_avx(insn)) {
		intel_pt_insn->op = INTEL_PT_OP_OTHER;
		intel_pt_insn->branch = INTEL_PT_BR_NO_BRANCH;
		intel_pt_insn->length = insn->length;
		return;
	}

	switch (insn->opcode.bytes[0]) {
	case 0xf:
		switch (insn->opcode.bytes[1]) {
		case 0x05: /* syscall */
		case 0x34: /* sysenter */
			op = INTEL_PT_OP_SYSCALL;
			branch = INTEL_PT_BR_INDIRECT;
			break;
		case 0x07: /* sysret */
		case 0x35: /* sysexit */
			op = INTEL_PT_OP_SYSRET;
			branch = INTEL_PT_BR_INDIRECT;
			break;
		case 0x80 ... 0x8f: /* jcc */
			op = INTEL_PT_OP_JCC;
			branch = INTEL_PT_BR_CONDITIONAL;
			break;
		default:
			break;
		}
		break;
	case 0x70 ... 0x7f: /* jcc */
		op = INTEL_PT_OP_JCC;
		branch = INTEL_PT_BR_CONDITIONAL;
		break;
	case 0xc2: /* near ret */
	case 0xc3: /* near ret */
	case 0xca: /* far ret */
	case 0xcb: /* far ret */
		op = INTEL_PT_OP_RET;
		branch = INTEL_PT_BR_INDIRECT;
		break;
	case 0xcf: /* iret */
		op = INTEL_PT_OP_IRET;
		branch = INTEL_PT_BR_INDIRECT;
		break;
	case 0xcc ... 0xce: /* int */
		op = INTEL_PT_OP_INT;
		branch = INTEL_PT_BR_INDIRECT;
		break;
	case 0xe8: /* call near rel */
		op = INTEL_PT_OP_CALL;
		branch = INTEL_PT_BR_UNCONDITIONAL;
		break;
	case 0x9a: /* call far absolute */
		op = INTEL_PT_OP_CALL;
		branch = INTEL_PT_BR_INDIRECT;
		break;
	case 0xe0 ... 0xe2: /* loop */
		op = INTEL_PT_OP_LOOP;
		branch = INTEL_PT_BR_CONDITIONAL;
		break;
	case 0xe3: /* jcc */
		op = INTEL_PT_OP_JCC;
		branch = INTEL_PT_BR_CONDITIONAL;
		break;
	case 0xe9: /* jmp */
	case 0xeb: /* jmp */
		op = INTEL_PT_OP_JMP;
		branch = INTEL_PT_BR_UNCONDITIONAL;
		break;
	case 0xea: /* far jmp */
		op = INTEL_PT_OP_JMP;
		branch = INTEL_PT_BR_INDIRECT;
		break;
	case 0xff: /* call near absolute, call far absolute ind */
		ext = (insn->modrm.bytes[0] >> 3) & 0x7;
		switch (ext) {
		case 2: /* near ind call */
		case 3: /* far ind call */
			op = INTEL_PT_OP_CALL;
			branch = INTEL_PT_BR_INDIRECT;
			break;
		case 4:
		case 5:
			op = INTEL_PT_OP_JMP;
			branch = INTEL_PT_BR_INDIRECT;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	intel_pt_insn->op = op;
	intel_pt_insn->branch = branch;
	intel_pt_insn->length = insn->length;

	if (branch == INTEL_PT_BR_CONDITIONAL ||
	    branch == INTEL_PT_BR_UNCONDITIONAL) {
#if __BYTE_ORDER == __BIG_ENDIAN
		switch (insn->immediate.nbytes) {
		case 1:
			intel_pt_insn->rel = insn->immediate.value;
			break;
		case 2:
			intel_pt_insn->rel =
					bswap_16((short)insn->immediate.value);
			break;
		case 4:
			intel_pt_insn->rel = bswap_32(insn->immediate.value);
			break;
		default:
			intel_pt_insn->rel = 0;
			break;
		}
#else
		intel_pt_insn->rel = insn->immediate.value;
#endif
	}
}

int intel_pt_get_insn(const unsigned char *buf, size_t len, int x86_64,
		      struct intel_pt_insn *intel_pt_insn)
{
	struct insn insn;

	insn_init(&insn, buf, len, x86_64);
	insn_get_length(&insn);
	if (!insn_complete(&insn) || insn.length > len)
		return -1;
	intel_pt_insn_decoder(&insn, intel_pt_insn);
	if (insn.length < INTEL_PT_INSN_BUF_SZ)
		memcpy(intel_pt_insn->buf, buf, insn.length);
	else
		memcpy(intel_pt_insn->buf, buf, INTEL_PT_INSN_BUF_SZ);
	return 0;
}

const char *branch_name[] = {
	[INTEL_PT_OP_OTHER]	= "Other",
	[INTEL_PT_OP_CALL]	= "Call",
	[INTEL_PT_OP_RET]	= "Ret",
	[INTEL_PT_OP_JCC]	= "Jcc",
	[INTEL_PT_OP_JMP]	= "Jmp",
	[INTEL_PT_OP_LOOP]	= "Loop",
	[INTEL_PT_OP_IRET]	= "IRet",
	[INTEL_PT_OP_INT]	= "Int",
	[INTEL_PT_OP_SYSCALL]	= "Syscall",
	[INTEL_PT_OP_SYSRET]	= "Sysret",
};

const char *intel_pt_insn_name(enum intel_pt_insn_op op)
{
	return branch_name[op];
}

int intel_pt_insn_desc(const struct intel_pt_insn *intel_pt_insn, char *buf,
		       size_t buf_len)
{
	switch (intel_pt_insn->branch) {
	case INTEL_PT_BR_CONDITIONAL:
	case INTEL_PT_BR_UNCONDITIONAL:
		return snprintf(buf, buf_len, "%s %s%d",
				intel_pt_insn_name(intel_pt_insn->op),
				intel_pt_insn->rel > 0 ? "+" : "",
				intel_pt_insn->rel);
	case INTEL_PT_BR_NO_BRANCH:
	case INTEL_PT_BR_INDIRECT:
		return snprintf(buf, buf_len, "%s",
				intel_pt_insn_name(intel_pt_insn->op));
	default:
		break;
	}
	return 0;
}

int intel_pt_insn_type(enum intel_pt_insn_op op)
{
	switch (op) {
	case INTEL_PT_OP_OTHER:
		return 0;
	case INTEL_PT_OP_CALL:
		return PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL;
	case INTEL_PT_OP_RET:
		return PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_RETURN;
	case INTEL_PT_OP_JCC:
		return PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CONDITIONAL;
	case INTEL_PT_OP_JMP:
		return PERF_IP_FLAG_BRANCH;
	case INTEL_PT_OP_LOOP:
		return PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CONDITIONAL;
	case INTEL_PT_OP_IRET:
		return PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_RETURN |
		       PERF_IP_FLAG_INTERRUPT;
	case INTEL_PT_OP_INT:
		return PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL |
		       PERF_IP_FLAG_INTERRUPT;
	case INTEL_PT_OP_SYSCALL:
		return PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_CALL |
		       PERF_IP_FLAG_SYSCALLRET;
	case INTEL_PT_OP_SYSRET:
		return PERF_IP_FLAG_BRANCH | PERF_IP_FLAG_RETURN |
		       PERF_IP_FLAG_SYSCALLRET;
	default:
		return 0;
	}
}

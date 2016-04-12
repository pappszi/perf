/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * Copyright (C) 2015 Naveen N. Rao, IBM Corporation
 */

#include "debug.h"
#include "symbol.h"
#include "map.h"
#include "probe-event.h"

#ifdef HAVE_LIBELF_SUPPORT
bool elf__needs_adjust_symbols(GElf_Ehdr ehdr)
{
	return ehdr.e_type == ET_EXEC ||
	       ehdr.e_type == ET_REL ||
	       ehdr.e_type == ET_DYN;
}

#if defined(_CALL_ELF) && _CALL_ELF == 2
void arch__elf_sym_adjust(GElf_Sym *sym)
{
	sym->st_value += PPC64_LOCAL_ENTRY_OFFSET(sym->st_other);
}
#endif
#endif

#if !defined(_CALL_ELF) || _CALL_ELF != 2
int arch__choose_best_symbol(struct symbol *syma,
			     struct symbol *symb __maybe_unused)
{
	char *sym = syma->name;

	/* Skip over any initial dot */
	if (*sym == '.')
		sym++;

	/* Avoid "SyS" kernel syscall aliases */
	if (strlen(sym) >= 3 && !strncmp(sym, "SyS", 3))
		return SYMBOL_B;
	if (strlen(sym) >= 10 && !strncmp(sym, "compat_SyS", 10))
		return SYMBOL_B;

	return SYMBOL_A;
}

/* Allow matching against dot variants */
int arch__compare_symbol_names(const char *namea, const char *nameb)
{
	/* Skip over initial dot */
	if (*namea == '.')
		namea++;
	if (*nameb == '.')
		nameb++;

	return strcmp(namea, nameb);
}
#endif

#if defined(_CALL_ELF) && _CALL_ELF == 2
bool arch__prefers_symtab(void)
{
	return true;
}

#define PPC64LE_LEP_OFFSET	8

void arch__fix_tev_from_maps(struct perf_probe_event *pev,
			     struct probe_trace_event *tev, struct map *map)
{
	/*
	 * When probing at a function entry point, we normally always want the
	 * LEP since that catches calls to the function through both the GEP and
	 * the LEP. Hence, we would like to probe at an offset of 8 bytes if
	 * the user only specified the function entry.
	 *
	 * However, if the user specifies an offset, we fall back to using the
	 * GEP since all userspace applications (objdump/readelf) show function
	 * disassembly with offsets from the GEP.
	 *
	 * In addition, we shouldn't specify an offset for kretprobes.
	 */
	if (pev->point.offset || pev->point.retprobe || !map)
		return;

	if (map->dso->symtab_type == DSO_BINARY_TYPE__KALLSYMS)
		tev->point.offset += PPC64LE_LEP_OFFSET;
}
#endif

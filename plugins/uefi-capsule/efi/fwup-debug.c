/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include <efi.h>
#include <efilib.h>

#include "fwup-cleanups.h"
#include "fwup-debug.h"
#include "fwup-efi.h"

static BOOLEAN debugging = FALSE;

BOOLEAN
fwup_debug_get_enabled(VOID)
{
	return debugging;
}

VOID
fwup_debug_set_enabled(BOOLEAN val)
{
	debugging = val;
}

static VOID
fwupd_debug_efivar_append(CHAR16 *out1)
{
	CHAR16 *name = L"FWUPDATE_DEBUG_LOG";
	UINT32 attrs = EFI_VARIABLE_NON_VOLATILE |
			 EFI_VARIABLE_BOOTSERVICE_ACCESS |
			 EFI_VARIABLE_RUNTIME_ACCESS;
	static BOOLEAN once = TRUE;
	if (once) {
		once = FALSE;
		fwup_delete_variable(name, &fwupdate_guid);
	} else {
		attrs |= EFI_VARIABLE_APPEND_WRITE;
	}
	fwup_set_variable(name, &fwupdate_guid, out1, StrSize(out1) - sizeof(CHAR16), attrs);
}

VOID
fwup_log(FwupLogLevel level, const char *func, const char *file, const int line, CHAR16 *fmt, ...)
{
	va_list args;
	_cleanup_free CHAR16 *tmp = NULL;

	va_start(args, fmt);
	tmp = VPoolPrint(fmt, args);
	va_end(args);
	if (tmp == NULL) {
		Print(L"fwupdate: Allocation for debug log failed!\n");
		return;
	}

	if (debugging) {
		_cleanup_free CHAR16 *out1 = NULL;
		out1 = PoolPrint(L"%a:%d:%a(): %s\n", file, line, func, tmp);
		if (out1 == NULL) {
			Print(L"fwupdate: Allocation for debug log failed!\n");
			return;
		}
		Print(L"%s", out1);
		fwupd_debug_efivar_append(out1);
	} else {
		switch (level) {
		case FWUP_DEBUG_LEVEL_DEBUG:
			break;
		case FWUP_DEBUG_LEVEL_WARNING:
			Print(L"WARNING: %s\n", tmp);
			break;
		default:
			Print(L"%s\n", tmp);
			break;
		}
	}
}

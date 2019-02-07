/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include <efi.h>
#include <efilib.h>

#include "fwup-cleanups.h"
#include "fwup-common.h"
#include "fwup-debug.h"
#include "fwup-efi.h"

EFI_STATUS
fwup_delete_variable(CHAR16 *name, EFI_GUID *guid)
{
	EFI_STATUS rc;
	UINT32 attrs = 0;

	/* get the attrs so we can delete it */
	rc = uefi_call_wrapper(RT->GetVariable, 5, name, guid, &attrs, NULL, NULL);
	if (EFI_ERROR(rc)) {
		if (rc == EFI_NOT_FOUND) {
			fwup_debug(L"Not deleting variable '%s' as not found", name);
			return EFI_SUCCESS;
		}
		fwup_debug(L"Could not get variable '%s' for delete: %r", name, rc);
		return rc;
	}
	return uefi_call_wrapper(RT->SetVariable, 5, name, guid, attrs, 0, NULL);
}

EFI_STATUS
fwup_set_variable(CHAR16 *name, EFI_GUID *guid, VOID *data, UINTN size, UINT32 attrs)
{
	return uefi_call_wrapper(RT->SetVariable, 5, name, guid, attrs, size, data);
}

EFI_STATUS
fwup_get_variable(CHAR16 *name, EFI_GUID *guid, VOID **buf_out, UINTN *buf_size_out, UINT32 *attrs_out)
{
	EFI_STATUS rc;
	UINTN size = 0;
	UINT32 attrs;
	_cleanup_free VOID *buf = NULL;

	rc = uefi_call_wrapper(RT->GetVariable, 5, name, guid, &attrs, &size, NULL);
	if (EFI_ERROR(rc)) {
		if (rc == EFI_BUFFER_TOO_SMALL) {
			buf = fwup_malloc(size);
			if (buf == NULL)
				return EFI_OUT_OF_RESOURCES;
		} else if (rc != EFI_NOT_FOUND) {
			fwup_debug(L"Could not get variable '%s': %r", name, rc);
			return rc;
		}
	} else {
		fwup_debug(L"GetVariable(%s) succeeded with size=0", name);
		return EFI_INVALID_PARAMETER;
	}
	rc = uefi_call_wrapper(RT->GetVariable, 5, name, guid, &attrs, &size, buf);
	if (EFI_ERROR(rc)) {
		fwup_warning(L"Could not get variable '%s': %r", name, rc);
		return rc;
	}
	*buf_out = _steal_pointer(&buf);
	*buf_size_out = size;
	*attrs_out = attrs;
	return EFI_SUCCESS;
}

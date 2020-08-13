/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-efi-signature-common.h"
#include "fu-efi-signature-list.h"

static gboolean
fu_efi_signature_list_array_has_checksum (GPtrArray *siglists, const gchar *checksum)
{
	for (guint i = 0; i < siglists->len; i++) {
		FuEfiSignatureList *siglist = g_ptr_array_index (siglists, i);
		if (fu_efi_signature_list_has_checksum (siglist, checksum))
			return TRUE;
	}
	return FALSE;
}

gboolean
fu_efi_signature_list_array_inclusive (GPtrArray *outer, GPtrArray *inner)
{
	for (guint j = 0; j < inner->len; j++) {
		FuEfiSignatureList *siglist = g_ptr_array_index (inner, j);
		GPtrArray *items = fu_efi_signature_list_get_all (siglist);
		for (guint i = 0; i < items->len; i++) {
			FuEfiSignature *sig = g_ptr_array_index (items, i);
			const gchar *checksum = fu_efi_signature_get_checksum (sig);
			if (!fu_efi_signature_list_array_has_checksum (outer, checksum))
				return FALSE;
		}
	}
	return TRUE;
}

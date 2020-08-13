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

guint
fu_efi_signature_list_array_version (GPtrArray *siglists)
{
	guint csum_cnt = 0;
	const gchar *ignored_guids[] = {
		FU_EFI_SIGNATURE_GUID_OVMF,
		NULL };
	for (guint j = 0; j < siglists->len; j++) {
		FuEfiSignatureList *siglist = g_ptr_array_index (siglists, j);
		GPtrArray *items = fu_efi_signature_list_get_all (siglist);
		for (guint i = 0; i < items->len; i++) {
			FuEfiSignature *sig = g_ptr_array_index (items, i);
			if (fu_efi_signature_get_kind (sig) != FU_EFI_SIGNATURE_KIND_SHA256)
				continue;
			if (g_strv_contains (ignored_guids, fu_efi_signature_get_owner (sig)))
				continue;
			csum_cnt++;
		}
	}
	return csum_cnt;
}

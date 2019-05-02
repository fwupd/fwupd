/*
 * Copyright (C) 2018-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <efivar.h>

#include "fu-common.h"
#include "fu-uefi-devpath.h"

#include "fwupd-error.h"

typedef struct {
	guint8	 type;
	guint8	 subtype;
	GBytes	*data;
} FuUefiDevPath;

static void
fu_uefi_efi_dp_free (FuUefiDevPath *dp)
{
	if (dp->data != NULL)
		g_bytes_unref (dp->data);
	g_free (dp);
}

GBytes *
fu_uefi_devpath_find_data (GPtrArray *dps, guint8 type, guint8 subtype, GError **error)
{
	for (guint i = 0; i < dps->len; i++) {
		FuUefiDevPath *dp = g_ptr_array_index (dps, i);
		if (dp->type == type && dp->subtype == subtype)
			return dp->data;
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_INTERNAL,
		     "no DP with type 0x%02x and subtype 0x%02x",
		     type, subtype);
	return NULL;
}

GPtrArray *
fu_uefi_devpath_parse (const guint8 *buf, gsize sz,
		       FuUefiDevpathParseFlags flags, GError **error)
{
	guint16 offset = 0;
	g_autoptr(GPtrArray) dps = NULL;

	/* sanity check */
	if (sz < sizeof(efidp_header)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "const_efidp is corrupt");
		return NULL;
	}

	dps = g_ptr_array_new_with_free_func ((GDestroyNotify) fu_uefi_efi_dp_free);
	while (1) {
		FuUefiDevPath *dp;
		const efidp_header *hdr = (efidp_header *) (buf + offset);
		guint16 hdr_length = GUINT16_FROM_LE(hdr->length);

		/* check if last entry */
		g_debug ("DP type:0x%02x subtype:0x%02x size:0x%04x",
			 hdr->type, hdr->subtype, hdr->length);
		if (hdr->type == EFIDP_END_TYPE && hdr->subtype == EFIDP_END_ENTIRE)
			break;

		/* work around a bug in efi_va_generate_file_device_path_from_esp */
		if (offset + sizeof(efidp_header) + hdr->length > sz) {
			hdr_length = 0;
			fu_common_dump_full (G_LOG_DOMAIN, "efidp",
					     buf + offset, sz - offset, 32,
					     FU_DUMP_FLAGS_SHOW_ADDRESSES);
			for (guint16 i = offset + 4; i <= sz - 4; i++) {
				if (memcmp (buf + i, "\x7f\xff\x04\x00", 4) == 0) {
					hdr_length = i - offset;
					g_debug ("found END_ENTIRE at 0x%04x",
						 (guint) (i - offset));
					break;
				}
			}
			if (hdr_length == 0) {
				g_set_error_literal (error,
						     FWUPD_ERROR,
						     FWUPD_ERROR_INTERNAL,
						     "DP length invalid and no END_ENTIRE "
						     "found, possibly data truncation?");
				return NULL;
			}
			if ((flags & FU_UEFI_DEVPATH_PARSE_FLAG_REPAIR) == 0) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "DP length invalid, reported 0x%04x, maybe 0x%04x",
					     hdr->length, hdr_length);
				return NULL;
			}
			g_debug ("DP length invalid! Truncating from 0x%04x to 0x%04x",
				 hdr->length, hdr_length);
		}

		/* add new DP */
		dp = g_new0 (FuUefiDevPath, 1);
		dp->type = hdr->type;
		dp->subtype = hdr->subtype;
		if (hdr_length > 0)
			dp->data = g_bytes_new (buf + offset + 4, hdr_length);
		g_ptr_array_add (dps, dp);

		/* advance to next DP */
		offset += hdr_length;
		if (offset + sizeof(efidp_header) > sz) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "DP length invalid after fixing");
			return NULL;
		}

	}
	return g_steal_pointer (&dps);
}

/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <fwupd.h>

#include "fu-common.h"
#include "fu-efi-signature-parser.h"

static gboolean
fu_efi_signature_list_parse_item (FuEfiSignatureList *siglist,
				  const guint8 *buf,
				  gsize bufsz,
				  gsize offset,
				  guint32 sig_size,
				  GError **error)
{
	fwupd_guid_t guid;
	gsize sig_datasz = sig_size - sizeof(fwupd_guid_t);
	g_autofree gchar *sig_owner = NULL;
	g_autofree guint8 *sig_data = g_malloc0 (sig_datasz);
	g_autoptr(FuEfiSignature) sig = NULL;
	g_autoptr(GBytes) data = NULL;

	/* read both blocks of data */
	if (!fu_memcpy_safe ((guint8 *) &guid, sizeof(guid), 0x0,	/* dst */
			     buf, bufsz, offset,			/* src */
			     sizeof(guid), error)) {
		g_prefix_error (error, "failed to read signature GUID: ");
		return FALSE;
	}
	if (!fu_memcpy_safe (sig_data, sig_datasz, 0x0,			/* dst */
			     buf, bufsz, offset + sizeof(fwupd_guid_t),	/* src */
			     sig_datasz, error)) {
		g_prefix_error (error, "failed to read signature data: ");
		return FALSE;
	}

	/* create item */
	sig_owner = fwupd_guid_to_string (&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);
	data = g_bytes_new (sig_data, sig_datasz);
	sig = fu_efi_signature_new (fu_efi_signature_list_get_kind (siglist), sig_owner, data);
	fu_efi_signature_list_add (siglist, sig);
	return TRUE;
}

static gboolean
fu_efi_signature_list_parse_list (GPtrArray *siglists,
				  const guint8 *buf,
				  gsize bufsz,
				  gsize *offset,
				  GError **error)
{
	fwupd_guid_t guid;
	gsize offset_tmp;
	guint32 sig_header_size = 0;
	guint32 sig_list_size = 0;
	guint32 sig_size = 0;
	g_autofree gchar *sig_type = NULL;
	g_autoptr(FuEfiSignatureList) siglist = NULL;

	/* read EFI_SIGNATURE_LIST */
	if (!fu_memcpy_safe ((guint8 *) &guid, sizeof(guid), 0x0,	/* dst */
			     buf, bufsz, *offset,			/* src */
			     sizeof(guid), error)) {
		g_prefix_error (error, "failed to read GUID header: ");
		return FALSE;
	}
	sig_type = fwupd_guid_to_string (&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);
	if (g_strcmp0 (sig_type, "c1c41626-504c-4092-aca9-41f936934328") == 0) {
		g_debug ("EFI_SIGNATURE_LIST SHA256");
		siglist = fu_efi_signature_list_new (FU_EFI_SIGNATURE_KIND_SHA256);
	} else if (g_strcmp0 (sig_type, "a5c059a1-94e4-4aa7-87b5-ab155c2bf072") == 0) {
		g_debug ("EFI_SIGNATURE_LIST X509");
		siglist = fu_efi_signature_list_new (FU_EFI_SIGNATURE_KIND_X509);
	} else {
		g_debug ("EFI_SIGNATURE_LIST unknown: %s", sig_type);
		siglist = fu_efi_signature_list_new (FU_EFI_SIGNATURE_KIND_UNKNOWN);
	}
	if (!fu_common_read_uint32_safe (buf, bufsz, *offset + 0x10,
					 &sig_list_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (sig_list_size < 0x1c || sig_list_size > 1024 * 1024) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "SignatureListSize invalid: 0x%x", sig_list_size);
		return FALSE;
	}
	if (!fu_common_read_uint32_safe (buf, bufsz, *offset + 0x14,
					 &sig_header_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (sig_header_size > 1024 * 1024) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "SignatureHeaderSize invalid: 0x%x", sig_size);
		return FALSE;
	}
	if (!fu_common_read_uint32_safe (buf, bufsz, *offset + 0x18,
					 &sig_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (sig_size < sizeof(fwupd_guid_t) || sig_size > 1024 * 1024) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "SignatureSize invalid: 0x%x", sig_size);
		return FALSE;
	}

	/* header is typically unused */
	offset_tmp = *offset + 0x1c + sig_header_size;
	for (guint i = 0; i < (sig_list_size - 0x1c) / sig_size; i++) {
		if (!fu_efi_signature_list_parse_item (siglist, buf, bufsz,
						       offset_tmp, sig_size,
						       error))
			return FALSE;
		offset_tmp += sig_size;
	}
	*offset += sig_list_size;
	g_ptr_array_add (siglists, g_steal_pointer (&siglist));
	return TRUE;
}

GPtrArray *
fu_efi_signature_parser_new (const guint8 *buf, gsize bufsz,
			     FuEfiSignatureParserFlags flags,
			     GError **error)
{
	gsize offset_fs = 0;
	g_autoptr(GPtrArray) siglists = NULL;

	/* this allows us to skip the efi permissions uint32_t or even the
	 * Microsoft PKCS-7 signature */
	if (flags & FU_EFI_SIGNATURE_PARSER_FLAGS_IGNORE_HEADER) {
		for (gsize i = 0; i < bufsz - 5; i++) {
			if (memcmp (buf + i, "\x26\x16\xc4\xc1\x4c", 5) == 0) {
				g_debug ("found EFI_SIGNATURE_LIST @0x%x", (guint) i);
				offset_fs = i;
				break;
			}
		}
	}

	/* parse each EFI_SIGNATURE_LIST */
	siglists = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	for (gsize offset = offset_fs; offset < bufsz;) {
		if (!fu_efi_signature_list_parse_list (siglists, buf, bufsz, &offset, error))
			return NULL;
	}

	/* success */
	return g_steal_pointer (&siglists);
}

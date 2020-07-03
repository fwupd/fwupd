/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <fwupd.h>

#include "fu-common.h"
#include "fu-uefi-dbx-common.h"
#include "fu-uefi-dbx-file.h"

struct _FuUefiDbxFile {
	GObject		 parent_instance;
	GPtrArray	*checksums;
};

G_DEFINE_TYPE (FuUefiDbxFile, fu_uefi_dbx_file, G_TYPE_OBJECT)

static gboolean
fu_uefi_dbx_file_parse_sig_item (FuUefiDbxFile *self,
				 const guint8 *buf,
				 gsize bufsz,
				 gsize offset,
				 guint32 sig_size,
				 GError **error)
{
	GString *sig_datastr;
	fwupd_guid_t guid;
	gsize sig_datasz = sig_size - sizeof(fwupd_guid_t);
	g_autofree gchar *sig_owner = NULL;
	g_autofree guint8 *sig_data = g_malloc0 (sig_datasz);

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

	/* we don't care about the owner, so just store the checksum */
	sig_owner = fwupd_guid_to_string (&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);
	sig_datastr = g_string_new (NULL);
	for (gsize j = 0; j < sig_datasz; j++)
		g_string_append_printf (sig_datastr, "%02x", sig_data[j]);
	if (g_getenv ("FWUPD_UEFI_DBX_VERBOSE") != NULL)
		g_debug ("Owner: %s, Data: %s", sig_owner, sig_datastr->str);
	g_ptr_array_add (self->checksums, g_string_free (sig_datastr, FALSE));
	return TRUE;
}

static gboolean
fu_uefi_dbx_file_parse_sig_list (FuUefiDbxFile *self,
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

	/* read EFI_SIGNATURE_LIST */
	if (!fu_memcpy_safe ((guint8 *) &guid, sizeof(guid), 0x0,	/* dst */
			     buf, bufsz, *offset,			/* src */
			     sizeof(guid), error)) {
		g_prefix_error (error, "failed to read GUID header: ");
		return FALSE;
	}
	sig_type = fwupd_guid_to_string (&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);
	if (g_strcmp0 (sig_type, "c1c41626-504c-4092-aca9-41f936934328") == 0)
		g_debug ("EFI_SIGNATURE_LIST SHA256");
	else if (g_strcmp0 (sig_type, "a5c059a1-94e4-4aa7-87b5-ab155c2bf072") == 0)
		g_debug ("EFI_SIGNATURE_LIST X509");
	else
		g_debug ("EFI_SIGNATURE_LIST unknown: %s", sig_type);
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
		if (!fu_uefi_dbx_file_parse_sig_item (self, buf, bufsz,
						      offset_tmp, sig_size,
						      error))
			return FALSE;
		offset_tmp += sig_size;
	}
	*offset += sig_list_size;
	return TRUE;
}

FuUefiDbxFile *
fu_uefi_dbx_file_new (const guint8 *buf, gsize bufsz,
		      FuUefiDbxFileParseFlags flags,
		      GError **error)
{
	gsize offset_fs = 0;
	g_autoptr(FuUefiDbxFile) self = g_object_new (FU_TYPE_UEFI_DBX_FILE, NULL);

	/* this allows us to skip the efi permissions uint32_t or even the
	 * Microsoft PKCS-7 signature */
	if (flags & FU_UEFI_DBX_FILE_PARSE_FLAGS_IGNORE_HEADER) {
		for (gsize i = 0; i < bufsz - 5; i++) {
			if (memcmp (buf + i, "\x26\x16\xc4\xc1\x4c", 5) == 0) {
				g_debug ("found EFI_SIGNATURE_LIST @0x%x", (guint) i);
				offset_fs = i;
				break;
			}
		}
	}

	/* parse each EFI_SIGNATURE_LIST */
	for (gsize offset = offset_fs; offset < bufsz;) {
		if (!fu_uefi_dbx_file_parse_sig_list (self, buf, bufsz, &offset, error))
			return NULL;
	}

	/* success */
	return g_steal_pointer (&self);
}

gboolean
fu_uefi_dbx_file_has_checksum (FuUefiDbxFile *self, const gchar *checksum)
{
	g_return_val_if_fail (FU_IS_UEFI_DBX_FILE (self), FALSE);
	for (guint i = 0; i < self->checksums->len; i++) {
		const gchar *checksums_tmp = g_ptr_array_index (self->checksums, i);
		if (g_strcmp0 (checksums_tmp, checksum) == 0)
			return TRUE;
	}
	return FALSE;
}

GPtrArray *
fu_uefi_dbx_file_get_checksums (FuUefiDbxFile *self)
{
	g_return_val_if_fail (FU_IS_UEFI_DBX_FILE (self), FALSE);
	return self->checksums;
}

static void
fu_uefi_dbx_file_finalize (GObject *obj)
{
	FuUefiDbxFile *self = FU_UEFI_DBX_FILE (obj);
	g_ptr_array_unref (self->checksums);
	G_OBJECT_CLASS (fu_uefi_dbx_file_parent_class)->finalize (obj);
}

static void
fu_uefi_dbx_file_class_init (FuUefiDbxFileClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_uefi_dbx_file_finalize;
}

static void
fu_uefi_dbx_file_init (FuUefiDbxFile *self)
{
	self->checksums = g_ptr_array_new_with_free_func (g_free);
}

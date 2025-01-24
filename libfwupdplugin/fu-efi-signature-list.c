/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEfiSignatureList"

#include "config.h"

#include <fwupd.h>
#include <string.h>

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-efi-signature-list.h"
#include "fu-efi-signature-private.h"
#include "fu-efi-struct.h"
#include "fu-input-stream.h"
#include "fu-mem.h"

/**
 * FuEfiSignatureList:
 *
 * A UEFI signature list typically found in the `PK` and `KEK` keys.
 *
 * See also: [class@FuFirmware]
 */

struct _FuEfiSignatureList {
	FuFirmware parent_instance;
};

G_DEFINE_TYPE(FuEfiSignatureList, fu_efi_signature_list, FU_TYPE_FIRMWARE)

const guint8 FU_EFI_SIGLIST_HEADER_MAGIC[] = {0x26, 0x16, 0xC4, 0xC1, 0x4C};

static gboolean
fu_efi_signature_list_parse_item(FuEfiSignatureList *self,
				 FuEfiSignatureKind sig_kind,
				 GInputStream *stream,
				 gsize offset,
				 guint32 size,
				 GError **error)
{
	fwupd_guid_t guid;
	g_autofree gchar *sig_owner = NULL;
	g_autoptr(FuEfiSignature) sig = NULL;
	g_autoptr(GBytes) data = NULL;

	/* allocate data buf */
	if (size <= sizeof(fwupd_guid_t)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "SignatureSize invalid: 0x%x",
			    (guint)size);
		return FALSE;
	}

	/* read both blocks of data */
	if (!fu_input_stream_read_safe(stream,
				       (guint8 *)&guid,
				       sizeof(guid),
				       0x0,
				       offset,
				       sizeof(guid),
				       error)) {
		g_prefix_error(error, "failed to read signature GUID: ");
		return FALSE;
	}
	data = fu_input_stream_read_bytes(stream,
					  offset + sizeof(fwupd_guid_t),
					  size - sizeof(fwupd_guid_t),
					  NULL,
					  error);
	if (data == NULL) {
		g_prefix_error(error, "failed to read signature data: ");
		return FALSE;
	}

	/* create item */
	sig_owner = fwupd_guid_to_string(&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);
	sig = fu_efi_signature_new(sig_kind, sig_owner);
	fu_firmware_set_bytes(FU_FIRMWARE(sig), data);
	return fu_firmware_add_image_full(FU_FIRMWARE(self), FU_FIRMWARE(sig), error);
}

static gboolean
fu_efi_signature_list_parse_list(FuEfiSignatureList *self,
				 GInputStream *stream,
				 gsize *offset,
				 GError **error)
{
	FuEfiSignatureKind sig_kind = FU_EFI_SIGNATURE_KIND_UNKNOWN;
	gsize offset_tmp;
	guint32 header_size;
	guint32 list_size;
	guint32 size;
	g_autofree gchar *sig_type = NULL;
	g_autoptr(GByteArray) st = NULL;

	/* read EFI_SIGNATURE_LIST */
	st = fu_struct_efi_signature_list_parse_stream(stream, *offset, error);
	if (st == NULL)
		return FALSE;
	sig_type = fwupd_guid_to_string(fu_struct_efi_signature_list_get_type(st),
					FWUPD_GUID_FLAG_MIXED_ENDIAN);
	if (g_strcmp0(sig_type, "c1c41626-504c-4092-aca9-41f936934328") == 0) {
		sig_kind = FU_EFI_SIGNATURE_KIND_SHA256;
	} else if (g_strcmp0(sig_type, "a5c059a1-94e4-4aa7-87b5-ab155c2bf072") == 0) {
		sig_kind = FU_EFI_SIGNATURE_KIND_X509;
	}
	list_size = fu_struct_efi_signature_list_get_list_size(st);
	if (list_size < 0x1c || list_size > 1024 * 1024) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "SignatureListSize invalid: 0x%x",
			    list_size);
		return FALSE;
	}
	header_size = fu_struct_efi_signature_list_get_header_size(st);
	if (header_size > 1024 * 1024) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "SignatureHeaderSize invalid: 0x%x",
			    header_size);
		return FALSE;
	}
	size = fu_struct_efi_signature_list_get_size(st);
	if (size < sizeof(fwupd_guid_t) || size > 1024 * 1024) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "SignatureSize invalid: 0x%x",
			    size);
		return FALSE;
	}

	/* header is typically unused */
	offset_tmp = *offset + 0x1c + header_size;
	for (guint i = 0; i < (list_size - 0x1c) / size; i++) {
		if (!fu_efi_signature_list_parse_item(self,
						      sig_kind,
						      stream,
						      offset_tmp,
						      size,
						      error))
			return FALSE;
		offset_tmp += size;
	}
	*offset += list_size;
	return TRUE;
}

static gboolean
fu_efi_signature_list_validate(FuFirmware *firmware,
			       GInputStream *stream,
			       gsize offset,
			       GError **error)
{
	fwupd_guid_t guid = {0x0};
	g_autofree gchar *sig_type = NULL;

	if (!fu_input_stream_read_safe(stream,
				       (guint8 *)&guid,
				       sizeof(guid),
				       0,
				       offset, /* seek */
				       sizeof(guid),
				       error)) {
		g_prefix_error(error, "failed to read magic: ");
		return FALSE;
	}
	sig_type = fwupd_guid_to_string(&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);
	if (g_strcmp0(sig_type, "c1c41626-504c-4092-aca9-41f936934328") != 0 &&
	    g_strcmp0(sig_type, "a5c059a1-94e4-4aa7-87b5-ab155c2bf072") != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid magic for file");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_efi_signature_list_parse(FuFirmware *firmware,
			    GInputStream *stream,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuEfiSignatureList *self = FU_EFI_SIGNATURE_LIST(firmware);
	gsize offset = 0;
	gsize streamsz = 0;

	/* parse each EFI_SIGNATURE_LIST */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	while (offset < streamsz) {
		if (!fu_efi_signature_list_parse_list(self, stream, &offset, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_efi_signature_list_write(FuFirmware *firmware, GError **error)
{
	fwupd_guid_t guid = {0};
	g_autoptr(FuStructEfiSignatureList) st = fu_struct_efi_signature_list_new();
	g_autoptr(GPtrArray) images = fu_firmware_get_images(firmware);

	/* entry */
	if (!fwupd_guid_from_string("c1c41626-504c-4092-aca9-41f936934328",
				    &guid,
				    FWUPD_GUID_FLAG_MIXED_ENDIAN,
				    error))
		return NULL;
	fu_struct_efi_signature_list_set_type(st, &guid);
	fu_struct_efi_signature_list_set_header_size(st, 0);
	fu_struct_efi_signature_list_set_list_size(st,
						   FU_STRUCT_EFI_SIGNATURE_LIST_SIZE +
						       (images->len * (16 + 32)));
	fu_struct_efi_signature_list_set_size(st, sizeof(fwupd_guid_t) + 32); /* SHA256 */

	/* SignatureOwner + SignatureData */
	for (guint i = 0; i < images->len; i++) {
		FuFirmware *img = g_ptr_array_index(images, i);
		g_autoptr(GBytes) img_blob = NULL;
		img_blob = fu_firmware_write(img, error);
		if (img_blob == NULL)
			return NULL;
		if (g_bytes_get_size(img_blob) != sizeof(fwupd_guid_t) + 32) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "expected SHA256 hash as signature data, got 0x%x",
				    (guint)(g_bytes_get_size(img_blob) - sizeof(fwupd_guid_t)));
			return NULL;
		}
		fu_byte_array_append_bytes(st, img_blob);
	}

	/* success */
	return g_steal_pointer(&st);
}

/**
 * fu_efi_signature_list_new:
 *
 * Creates a new #FuFirmware that can parse an EFI_SIGNATURE_LIST
 *
 * Since: 1.5.5
 **/
FuFirmware *
fu_efi_signature_list_new(void)
{
	return g_object_new(FU_TYPE_EFI_SIGNATURE_LIST, NULL);
}

static void
fu_efi_signature_list_class_init(FuEfiSignatureListClass *klass)
{
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	firmware_class->validate = fu_efi_signature_list_validate;
	firmware_class->parse = fu_efi_signature_list_parse;
	firmware_class->write = fu_efi_signature_list_write;
}

static void
fu_efi_signature_list_init(FuEfiSignatureList *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_ALWAYS_SEARCH);
	fu_firmware_set_images_max(FU_FIRMWARE(self), 2000);
	g_type_ensure(FU_TYPE_EFI_SIGNATURE);
}

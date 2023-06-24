/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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
				 const guint8 *buf,
				 gsize bufsz,
				 gsize offset,
				 guint32 size,
				 GError **error)
{
	fwupd_guid_t guid;
	gsize sig_datasz;
	g_autofree gchar *sig_owner = NULL;
	g_autofree guint8 *sig_data = NULL;
	g_autoptr(FuEfiSignature) sig = NULL;
	g_autoptr(GBytes) data = NULL;

	/* allocate data buf */
	if (size <= sizeof(fwupd_guid_t)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "SignatureSize invalid: 0x%x",
			    (guint)size);
		return FALSE;
	}
	sig_datasz = size - sizeof(fwupd_guid_t);
	sig_data = g_malloc0(sig_datasz);

	/* read both blocks of data */
	if (!fu_memcpy_safe((guint8 *)&guid,
			    sizeof(guid),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    offset, /* src */
			    sizeof(guid),
			    error)) {
		g_prefix_error(error, "failed to read signature GUID: ");
		return FALSE;
	}
	if (!fu_memcpy_safe(sig_data,
			    sig_datasz,
			    0x0, /* dst */
			    buf,
			    bufsz,
			    offset + sizeof(fwupd_guid_t), /* src */
			    sig_datasz,
			    error)) {
		g_prefix_error(error, "failed to read signature data: ");
		return FALSE;
	}

	/* create item */
	sig_owner = fwupd_guid_to_string(&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);
	data = g_bytes_new(sig_data, sig_datasz);
	sig = fu_efi_signature_new(sig_kind, sig_owner);
	fu_firmware_set_bytes(FU_FIRMWARE(sig), data);
	return fu_firmware_add_image_full(FU_FIRMWARE(self), FU_FIRMWARE(sig), error);
}

static gboolean
fu_efi_signature_list_parse_list(FuEfiSignatureList *self,
				 const guint8 *buf,
				 gsize bufsz,
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
	st = fu_struct_efi_signature_list_parse(buf, bufsz, *offset, error);
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
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "SignatureListSize invalid: 0x%x",
			    list_size);
		return FALSE;
	}
	header_size = fu_struct_efi_signature_list_get_header_size(st);
	if (header_size > 1024 * 1024) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "SignatureHeaderSize invalid: 0x%x",
			    header_size);
		return FALSE;
	}
	size = fu_struct_efi_signature_list_get_size(st);
	if (size < sizeof(fwupd_guid_t) || size > 1024 * 1024) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "SignatureSize invalid: 0x%x",
			    size);
		return FALSE;
	}

	/* header is typically unused */
	offset_tmp = *offset + 0x1c + header_size;
	for (guint i = 0; i < (list_size - 0x1c) / size; i++) {
		if (!fu_efi_signature_list_parse_item(self,
						      sig_kind,
						      buf,
						      bufsz,
						      offset_tmp,
						      size,
						      error))
			return FALSE;
		offset_tmp += size;
	}
	*offset += list_size;
	return TRUE;
}

static gchar *
fu_efi_signature_list_get_version(FuEfiSignatureList *self)
{
	guint csum_cnt = 0;
	const gchar *valid_owners[] = {FU_EFI_SIGNATURE_GUID_MICROSOFT, NULL};
	g_autoptr(GPtrArray) sigs = fu_firmware_get_images(FU_FIRMWARE(self));
	for (guint i = 0; i < sigs->len; i++) {
		FuEfiSignature *sig = g_ptr_array_index(sigs, i);
		if (fu_efi_signature_get_kind(sig) != FU_EFI_SIGNATURE_KIND_SHA256) {
			g_debug("ignoring dbx certificate in position %u", i);
			continue;
		}
		if (!g_strv_contains(valid_owners, fu_efi_signature_get_owner(sig))) {
			g_debug("ignoring non-Microsoft dbx hash: %s",
				fu_efi_signature_get_owner(sig));
			continue;
		}
		csum_cnt++;
	}
	return g_strdup_printf("%u", csum_cnt);
}

static gboolean
fu_efi_signature_list_check_magic(FuFirmware *firmware, GBytes *fw, gsize offset, GError **error)
{
	fwupd_guid_t guid = {0x0};
	g_autofree gchar *sig_type = NULL;

	/* read EFI_SIGNATURE_LIST */
	if (!fu_memcpy_safe((guint8 *)&guid,
			    sizeof(guid),
			    0x0, /* dst */
			    g_bytes_get_data(fw, NULL),
			    g_bytes_get_size(fw),
			    offset, /* src */
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
			    GBytes *fw,
			    gsize offset,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuEfiSignatureList *self = FU_EFI_SIGNATURE_LIST(firmware);
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autofree gchar *version_str = NULL;

	/* parse each EFI_SIGNATURE_LIST */
	while (offset < bufsz) {
		if (!fu_efi_signature_list_parse_list(self, buf, bufsz, &offset, error))
			return FALSE;
	}

	/* set version */
	version_str = fu_efi_signature_list_get_version(self);
	if (version_str != NULL)
		fu_firmware_set_version(firmware, version_str);

	/* success */
	return TRUE;
}

static GByteArray *
fu_efi_signature_list_write(FuFirmware *firmware, GError **error)
{
	g_autoptr(GByteArray) buf = fu_struct_efi_signature_list_new();

	/* entry */
	fu_struct_efi_signature_list_set_list_size(buf, buf->len + 16 + 32);
	fu_struct_efi_signature_list_set_header_size(buf, 0);
	fu_struct_efi_signature_list_set_size(buf, 16 + 32);

	/* SignatureOwner + SignatureData */
	for (guint i = 0; i < 16; i++)
		fu_byte_array_append_uint8(buf, '1');
	for (guint i = 0; i < 16; i++)
		fu_byte_array_append_uint8(buf, '2');

	return g_steal_pointer(&buf);
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
	FuFirmwareClass *klass_firmware = FU_FIRMWARE_CLASS(klass);
	klass_firmware->check_magic = fu_efi_signature_list_check_magic;
	klass_firmware->parse = fu_efi_signature_list_parse;
	klass_firmware->write = fu_efi_signature_list_write;
}

static void
fu_efi_signature_list_init(FuEfiSignatureList *self)
{
	fu_firmware_add_flag(FU_FIRMWARE(self), FU_FIRMWARE_FLAG_ALWAYS_SEARCH);
	fu_firmware_set_images_max(FU_FIRMWARE(self), 2000);
}

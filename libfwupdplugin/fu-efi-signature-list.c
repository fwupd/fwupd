/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>
#include <string.h>

#include "fu-byte-array.h"
#include "fu-common.h"
#include "fu-efi-signature-list.h"
#include "fu-efi-signature-private.h"
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

static gboolean
fu_efi_signature_list_parse_item(FuEfiSignatureList *self,
				 FuEfiSignatureKind sig_kind,
				 const guint8 *buf,
				 gsize bufsz,
				 gsize offset,
				 guint32 sig_size,
				 GError **error)
{
	fwupd_guid_t guid;
	gsize sig_datasz;
	g_autofree gchar *sig_owner = NULL;
	g_autofree guint8 *sig_data = NULL;
	g_autoptr(FuEfiSignature) sig = NULL;
	g_autoptr(GBytes) data = NULL;

	/* allocate data buf */
	if (sig_size <= sizeof(fwupd_guid_t)) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "SignatureSize invalid: 0x%x",
			    (guint)sig_size);
		return FALSE;
	}
	sig_datasz = sig_size - sizeof(fwupd_guid_t);
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
	fu_firmware_add_image(FU_FIRMWARE(self), FU_FIRMWARE(sig));
	return TRUE;
}

static gboolean
fu_efi_signature_list_parse_list(FuEfiSignatureList *self,
				 const guint8 *buf,
				 gsize bufsz,
				 gsize *offset,
				 GError **error)
{
	FuEfiSignatureKind sig_kind = FU_EFI_SIGNATURE_KIND_UNKNOWN;
	fwupd_guid_t guid;
	gsize offset_tmp;
	guint32 sig_header_size = 0;
	guint32 sig_list_size = 0;
	guint32 sig_size = 0;
	g_autofree gchar *sig_type = NULL;

	/* read EFI_SIGNATURE_LIST */
	if (!fu_memcpy_safe((guint8 *)&guid,
			    sizeof(guid),
			    0x0, /* dst */
			    buf,
			    bufsz,
			    *offset, /* src */
			    sizeof(guid),
			    error)) {
		g_prefix_error(error, "failed to read GUID header: ");
		return FALSE;
	}
	sig_type = fwupd_guid_to_string(&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);
	if (g_strcmp0(sig_type, "c1c41626-504c-4092-aca9-41f936934328") == 0) {
		sig_kind = FU_EFI_SIGNATURE_KIND_SHA256;
	} else if (g_strcmp0(sig_type, "a5c059a1-94e4-4aa7-87b5-ab155c2bf072") == 0) {
		sig_kind = FU_EFI_SIGNATURE_KIND_X509;
	}
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    *offset + 0x10,
				    &sig_list_size,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (sig_list_size < 0x1c || sig_list_size > 1024 * 1024) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "SignatureListSize invalid: 0x%x",
			    sig_list_size);
		return FALSE;
	}
	if (!fu_memread_uint32_safe(buf,
				    bufsz,
				    *offset + 0x14,
				    &sig_header_size,
				    G_LITTLE_ENDIAN,
				    error))
		return FALSE;
	if (sig_header_size > 1024 * 1024) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "SignatureHeaderSize invalid: 0x%x",
			    sig_size);
		return FALSE;
	}
	if (!fu_memread_uint32_safe(buf, bufsz, *offset + 0x18, &sig_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (sig_size < sizeof(fwupd_guid_t) || sig_size > 1024 * 1024) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "SignatureSize invalid: 0x%x",
			    sig_size);
		return FALSE;
	}

	/* header is typically unused */
	offset_tmp = *offset + 0x1c + sig_header_size;
	for (guint i = 0; i < (sig_list_size - 0x1c) / sig_size; i++) {
		if (!fu_efi_signature_list_parse_item(self,
						      sig_kind,
						      buf,
						      bufsz,
						      offset_tmp,
						      sig_size,
						      error))
			return FALSE;
		offset_tmp += sig_size;
	}
	*offset += sig_list_size;
	return TRUE;
}

static gchar *
fu_efi_signature_list_get_version(FuEfiSignatureList *self)
{
	guint csum_cnt = 0;
	const gchar *ignored_guids[] = {FU_EFI_SIGNATURE_GUID_OVMF,
					FU_EFI_SIGNATURE_GUID_OVMF_LEGACY,
					NULL};
	g_autoptr(GPtrArray) sigs = NULL;
	sigs = fu_firmware_get_images(FU_FIRMWARE(self));
	for (guint i = 0; i < sigs->len; i++) {
		FuEfiSignature *sig = g_ptr_array_index(sigs, i);
		if (fu_efi_signature_get_kind(sig) != FU_EFI_SIGNATURE_KIND_SHA256)
			continue;
		if (g_strv_contains(ignored_guids, fu_efi_signature_get_owner(sig)))
			continue;
		csum_cnt++;
	}
	return g_strdup_printf("%u", csum_cnt);
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

	/* this allows us to skip the efi permissions uint32_t or even the
	 * Microsoft PKCS-7 signature */
	if ((flags & FWUPD_INSTALL_FLAG_NO_SEARCH) == 0) {
		if (!fu_memmem_safe(buf,
				    bufsz,
				    (const guint8 *)"\x26\x16\xc4\xc1\x4c",
				    5,
				    &offset,
				    error))
			return FALSE;
		fu_firmware_set_offset(firmware, offset);
	}

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

static GBytes *
fu_efi_signature_list_write(FuFirmware *firmware, GError **error)
{
	GByteArray *buf = g_byte_array_new();

	/* SignatureType */
	for (guint i = 0; i < 16; i++)
		fu_byte_array_append_uint8(buf, 0x0);

	/* SignatureListSize */
	fu_byte_array_append_uint32(buf, 16 + 4 + 4 + 4 + 16 + 32, G_LITTLE_ENDIAN);

	/* SignatureHeaderSize */
	fu_byte_array_append_uint32(buf, 0, G_LITTLE_ENDIAN);

	/* SignatureSize */
	fu_byte_array_append_uint32(buf, 16 + 32, G_LITTLE_ENDIAN);

	/* SignatureOwner */
	for (guint i = 0; i < 16; i++)
		fu_byte_array_append_uint8(buf, '1');

	/* SignatureData */
	for (guint i = 0; i < 16; i++)
		fu_byte_array_append_uint8(buf, '2');

	return g_byte_array_free_to_bytes(buf);
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
	klass_firmware->parse = fu_efi_signature_list_parse;
	klass_firmware->write = fu_efi_signature_list_write;
}

static void
fu_efi_signature_list_init(FuEfiSignatureList *self)
{
}

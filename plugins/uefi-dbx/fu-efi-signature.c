/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-efi-signature.h"

struct _FuEfiSignature {
	FuFirmwareImage		 parent_instance;
	FuEfiSignatureKind	 kind;
	gchar			*owner;
};

G_DEFINE_TYPE (FuEfiSignature, fu_efi_signature, FU_TYPE_FIRMWARE_IMAGE)

const gchar *
fu_efi_signature_kind_to_string (FuEfiSignatureKind kind)
{
	if (kind == FU_EFI_SIGNATURE_KIND_SHA256)
		return "sha256";
	if (kind == FU_EFI_SIGNATURE_KIND_X509)
		return "x509_cert";
	return "unknown";
}

const gchar *
fu_efi_signature_guid_to_string (const gchar *guid)
{
	if (g_strcmp0 (guid, FU_EFI_SIGNATURE_GUID_ZERO) == 0)
		return "zero";
	if (g_strcmp0 (guid, FU_EFI_SIGNATURE_GUID_MICROSOFT) == 0)
		return "microsoft";
	if (g_strcmp0 (guid, FU_EFI_SIGNATURE_GUID_OVMF) == 0)
		return "ovmf";
	return guid;
}

FuEfiSignature *
fu_efi_signature_new (FuEfiSignatureKind kind, const gchar *owner)
{
	g_autoptr(FuEfiSignature) self = g_object_new (FU_TYPE_EFI_SIGNATURE, NULL);
	self->kind = kind;
	self->owner = g_strdup (owner);
	return g_steal_pointer (&self);
}

FuEfiSignatureKind
fu_efi_signature_get_kind (FuEfiSignature *self)
{
	g_return_val_if_fail (FU_IS_EFI_SIGNATURE (self), 0);
	return self->kind;
}

const gchar *
fu_efi_signature_get_owner (FuEfiSignature *self)
{
	g_return_val_if_fail (FU_IS_EFI_SIGNATURE (self), NULL);
	return self->owner;
}

static gchar *
fu_efi_signature_get_checksum (FuFirmwareImage *firmware_image,
			       GChecksumType csum_kind,
			       GError **error)
{
	FuEfiSignature *self = FU_EFI_SIGNATURE (firmware_image);
	g_autoptr(GBytes) data = fu_firmware_image_get_bytes (firmware_image);

	/* special case: this is *literally* a hash */
	if (self->kind == FU_EFI_SIGNATURE_KIND_SHA256 &&
	    csum_kind == G_CHECKSUM_SHA256) {
		GString *str;
		const guint8 *buf;
		gsize bufsz = 0;
		buf = g_bytes_get_data (data, &bufsz);
		str = g_string_new (NULL);
		for (gsize i = 0; i < bufsz; i++)
			g_string_append_printf (str, "%02x", buf[i]);
		return g_string_free (str, FALSE);
	}

	/* fallback */
	return g_compute_checksum_for_bytes (csum_kind, data);
}

static void
fu_efi_signature_finalize (GObject *obj)
{
	FuEfiSignature *self = FU_EFI_SIGNATURE (obj);
	g_free (self->owner);
	G_OBJECT_CLASS (fu_efi_signature_parent_class)->finalize (obj);
}

static void
fu_efi_signature_class_init (FuEfiSignatureClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuFirmwareImageClass *firmware_image_class = FU_FIRMWARE_IMAGE_CLASS (klass);
	object_class->finalize = fu_efi_signature_finalize;
	firmware_image_class->get_checksum = fu_efi_signature_get_checksum;
}

static void
fu_efi_signature_init (FuEfiSignature *self)
{
}

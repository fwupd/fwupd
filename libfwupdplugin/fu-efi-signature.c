/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-efi-signature-private.h"

/**
 * FuEfiSignature:
 *
 * A UEFI Signature as found in an `EFI_SIGNATURE_LIST`.
 *
 * See also: [class@FuFirmware]
 */

struct _FuEfiSignature {
	FuFirmware		 parent_instance;
	FuEfiSignatureKind	 kind;
	gchar			*owner;
};

G_DEFINE_TYPE (FuEfiSignature, fu_efi_signature, FU_TYPE_FIRMWARE)

/**
 * fu_efi_signature_kind_to_string:
 * @kind: A #FuEfiSignatureKind, e.g. %FU_EFI_SIGNATURE_KIND_X509
 *
 * Converts the signature kind to a text representation.
 *
 * Returns: text, e.g. `x509_cert`
 *
 * Since: 1.5.5
 **/
const gchar *
fu_efi_signature_kind_to_string (FuEfiSignatureKind kind)
{
	if (kind == FU_EFI_SIGNATURE_KIND_SHA256)
		return "sha256";
	if (kind == FU_EFI_SIGNATURE_KIND_X509)
		return "x509_cert";
	return "unknown";
}

/**
 * fu_efi_signature_new: (skip):
 * @kind: A #FuEfiSignatureKind
 * @owner: A GUID, e.g. %FU_EFI_SIGNATURE_GUID_MICROSOFT
 *
 * Creates a new EFI_SIGNATURE.
 *
 * Returns: (transfer full): signature
 *
 * Since: 1.5.5
 **/
FuEfiSignature *
fu_efi_signature_new (FuEfiSignatureKind kind, const gchar *owner)
{
	g_autoptr(FuEfiSignature) self = g_object_new (FU_TYPE_EFI_SIGNATURE, NULL);
	self->kind = kind;
	self->owner = g_strdup (owner);
	return g_steal_pointer (&self);
}

/**
 * fu_efi_signature_get_kind:
 * @self: A #FuEfiSignature
 *
 * Returns the signature kind.
 *
 * Returns: #FuEfiSignatureKind, e.g. %FU_EFI_SIGNATURE_KIND_SHA256
 *
 * Since: 1.5.5
 **/
FuEfiSignatureKind
fu_efi_signature_get_kind (FuEfiSignature *self)
{
	g_return_val_if_fail (FU_IS_EFI_SIGNATURE (self), FU_EFI_SIGNATURE_KIND_UNKNOWN);
	return self->kind;
}

/**
 * fu_efi_signature_get_owner:
 * @self: A #FuEfiSignature
 *
 * Returns the GUID of the signature owner.
 *
 * Returns: GUID owner, perhaps %FU_EFI_SIGNATURE_GUID_MICROSOFT
 *
 * Since: 1.5.5
 **/
const gchar *
fu_efi_signature_get_owner (FuEfiSignature *self)
{
	g_return_val_if_fail (FU_IS_EFI_SIGNATURE (self), NULL);
	return self->owner;
}

static gchar *
fu_efi_signature_get_checksum (FuFirmware *firmware,
			       GChecksumType csum_kind,
			       GError **error)
{
	FuEfiSignature *self = FU_EFI_SIGNATURE (firmware);
	g_autoptr(GBytes) data = fu_firmware_get_bytes (firmware, error);
	if (data == NULL)
		return NULL;

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
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS (klass);
	object_class->finalize = fu_efi_signature_finalize;
	firmware_class->get_checksum = fu_efi_signature_get_checksum;
}

static void
fu_efi_signature_init (FuEfiSignature *self)
{
}

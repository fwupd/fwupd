/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-efi-signature.h"

struct _FuEfiSignature {
	GObject			 parent_instance;
	FuEfiSignatureKind	 kind;
	gchar			*owner;
	gchar			*checksum; /* (nullable) */
	GBytes			*data;
};

G_DEFINE_TYPE (FuEfiSignature, fu_efi_signature, G_TYPE_OBJECT)


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
fu_efi_signature_new (FuEfiSignatureKind kind, const gchar *owner, GBytes *data)
{
	g_autoptr(FuEfiSignature) self = g_object_new (FU_TYPE_EFI_SIGNATURE, NULL);
	self->kind = kind;
	self->owner = g_strdup (owner);
	self->data = g_bytes_ref (data);
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

GBytes *
fu_efi_signature_get_data (FuEfiSignature *self)
{
	g_return_val_if_fail (FU_IS_EFI_SIGNATURE (self), NULL);
	return self->data;
}

const gchar *
fu_efi_signature_get_checksum (FuEfiSignature *self)
{
	g_return_val_if_fail (FU_IS_EFI_SIGNATURE (self), NULL);

	/* only create when required */
	if (self->checksum == NULL) {
		if (self->kind == FU_EFI_SIGNATURE_KIND_SHA256) {
			GString *str;
			const guint8 *buf;
			gsize bufsz = 0;
			buf = g_bytes_get_data (self->data, &bufsz);
			str = g_string_new (NULL);
			for (gsize i = 0; i < bufsz; i++)
				g_string_append_printf (str, "%02x", buf[i]);
			self->checksum = g_string_free (str, FALSE);
		} else {
			self->checksum = g_compute_checksum_for_bytes (G_CHECKSUM_SHA256, self->data);
		}
	}
	return self->checksum;
}

static void
fu_efi_signature_finalize (GObject *obj)
{
	FuEfiSignature *self = FU_EFI_SIGNATURE (obj);
	g_free (self->owner);
	g_free (self->checksum);
	g_bytes_unref (self->data);
	G_OBJECT_CLASS (fu_efi_signature_parent_class)->finalize (obj);
}

static void
fu_efi_signature_class_init (FuEfiSignatureClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_efi_signature_finalize;
}

static void
fu_efi_signature_init (FuEfiSignature *self)
{
}

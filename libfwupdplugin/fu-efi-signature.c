/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-efi-signature-private.h"

/**
 * FuEfiSignature:
 *
 * A UEFI Signature as found in an `EFI_SIGNATURE_LIST`.
 *
 * See also: [class@FuFirmware]
 */

struct _FuEfiSignature {
	FuFirmware parent_instance;
	FuEfiSignatureKind kind;
	gchar *owner;
};

G_DEFINE_TYPE(FuEfiSignature, fu_efi_signature, FU_TYPE_FIRMWARE)

static void
fu_efi_signature_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuEfiSignature *self = FU_EFI_SIGNATURE(firmware);
	fu_xmlb_builder_insert_kv(bn, "kind", fu_efi_signature_kind_to_string(self->kind));
	fu_xmlb_builder_insert_kv(bn, "owner", self->owner);

	/* special case: this is *literally* a hash */
	if (self->kind == FU_EFI_SIGNATURE_KIND_SHA256) {
		g_autoptr(GBytes) blob = fu_firmware_get_bytes(firmware, NULL);
		if (blob != NULL) {
			g_autofree gchar *str = fu_bytes_to_string(blob);
			fu_xmlb_builder_insert_kv(bn, "checksum", str);
		}
	}
}

/**
 * fu_efi_signature_new: (skip):
 * @kind: A #FuEfiSignatureKind
 *
 * Creates a new EFI_SIGNATURE.
 *
 * Returns: (transfer full): signature
 *
 * Since: 2.0.5
 **/
FuEfiSignature *
fu_efi_signature_new(FuEfiSignatureKind kind)
{
	g_autoptr(FuEfiSignature) self = g_object_new(FU_TYPE_EFI_SIGNATURE, NULL);
	self->kind = kind;
	return g_steal_pointer(&self);
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
fu_efi_signature_get_kind(FuEfiSignature *self)
{
	g_return_val_if_fail(FU_IS_EFI_SIGNATURE(self), FU_EFI_SIGNATURE_KIND_UNKNOWN);
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
fu_efi_signature_get_owner(FuEfiSignature *self)
{
	g_return_val_if_fail(FU_IS_EFI_SIGNATURE(self), NULL);
	return self->owner;
}

static gboolean
fu_efi_signature_parse(FuFirmware *firmware,
		       GInputStream *stream,
		       FwupdInstallFlags flags,
		       GError **error)
{
	FuEfiSignature *self = FU_EFI_SIGNATURE(firmware);
	fwupd_guid_t guid = {0};
	gsize size = fu_firmware_get_size(firmware);
	g_autoptr(GBytes) data = NULL;

	/* sanity check */
	if (size <= sizeof(fwupd_guid_t)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "SignatureSize invalid: 0x%x",
			    (guint)size);
		return FALSE;
	}

	/* GUID */
	if (!fu_input_stream_read_safe(stream,
				       (guint8 *)&guid,
				       sizeof(guid),
				       0x0, /* seek to */
				       0x0, /* offset */
				       sizeof(guid),
				       error)) {
		g_prefix_error(error, "failed to read signature GUID: ");
		return FALSE;
	}
	self->owner = fwupd_guid_to_string(&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);

	/* if data size is unspecified, we'll use the stream size */
	data = fu_input_stream_read_bytes(stream,
					  sizeof(fwupd_guid_t),
					  size - sizeof(fwupd_guid_t),
					  NULL,
					  error);
	if (data == NULL) {
		g_prefix_error(error, "failed to read signature data: ");
		return FALSE;
	}
	fu_firmware_set_bytes(firmware, data);

	/* success */
	return TRUE;
}

static GByteArray *
fu_efi_signature_write(FuFirmware *firmware, GError **error)
{
	FuEfiSignature *self = FU_EFI_SIGNATURE(firmware);
	fwupd_guid_t owner = {0};
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) data = NULL;

	/* optional owner */
	if (self->owner != NULL) {
		if (!fwupd_guid_from_string(self->owner,
					    &owner,
					    FWUPD_GUID_FLAG_MIXED_ENDIAN,
					    error))
			return NULL;
	}
	g_byte_array_append(buf, owner, sizeof(owner));

	/* data */
	data = fu_firmware_get_bytes_with_patches(firmware, error);
	if (data == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, data);

	/* success */
	return g_steal_pointer(&buf);
}

static gboolean
fu_efi_signature_build(FuFirmware *firmware, XbNode *n, GError **error)
{
	FuEfiSignature *self = FU_EFI_SIGNATURE(firmware);
	const gchar *tmp;

	/* optional properties */
	tmp = xb_node_query_text(n, "kind", NULL);
	if (tmp != NULL) {
		self->kind = fu_efi_signature_kind_from_string(tmp);
		if (self->kind == FU_EFI_SIGNATURE_KIND_UNKNOWN) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid kind: %s",
				    tmp);
			return FALSE;
		}
	}
	tmp = xb_node_query_text(n, "owner", NULL);
	if (tmp != NULL) {
		if (!fwupd_guid_from_string(tmp, NULL, FWUPD_GUID_FLAG_MIXED_ENDIAN, error)) {
			g_prefix_error(error, "failed to parse owner %s, expected GUID: ", tmp);
			return FALSE;
		}
		g_free(self->owner);
		self->owner = g_strdup(tmp);
	}
	tmp = xb_node_query_text(n, "checksum", NULL);
	if (tmp != NULL) {
		g_autoptr(GBytes) data = NULL;
		data = fu_bytes_from_string(tmp, error);
		if (data == NULL)
			return FALSE;
		fu_firmware_set_bytes(firmware, data);
	}

	/* success */
	return TRUE;
}

static gchar *
fu_efi_signature_get_checksum(FuFirmware *firmware, GChecksumType csum_kind, GError **error)
{
	FuEfiSignature *self = FU_EFI_SIGNATURE(firmware);
	g_autoptr(GBytes) data = fu_firmware_get_bytes_with_patches(firmware, error);
	if (data == NULL)
		return NULL;

	/* special case: this is *literally* a hash */
	if (self->kind == FU_EFI_SIGNATURE_KIND_SHA256 && csum_kind == G_CHECKSUM_SHA256)
		return fu_bytes_to_string(data);

	/* fallback */
	return g_compute_checksum_for_bytes(csum_kind, data);
}

static void
fu_efi_signature_finalize(GObject *obj)
{
	FuEfiSignature *self = FU_EFI_SIGNATURE(obj);
	g_free(self->owner);
	G_OBJECT_CLASS(fu_efi_signature_parent_class)->finalize(obj);
}

static void
fu_efi_signature_class_init(FuEfiSignatureClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_efi_signature_finalize;
	firmware_class->export = fu_efi_signature_export;
	firmware_class->parse = fu_efi_signature_parse;
	firmware_class->write = fu_efi_signature_write;
	firmware_class->build = fu_efi_signature_build;
	firmware_class->get_checksum = fu_efi_signature_get_checksum;
}

static void
fu_efi_signature_init(FuEfiSignature *self)
{
	self->kind = FU_EFI_SIGNATURE_KIND_SHA256;
}

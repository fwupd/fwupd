/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-efi-common.h"
#include "fu-efi-signature-private.h"
#include "fu-efi-struct.h"
#include "fu-input-stream.h"
#include "fu-version-common.h"

/**
 * FuEfiSignature:
 *
 * A UEFI Signature as found in an `EFI_SIGNATURE_LIST`.
 *
 * See also: [class@FuFirmware]
 */

typedef struct {
	FuEfiSignatureKind kind;
	gchar *owner;
	gchar *svn_guid; /* only for FU_EFI_SIGNATURE_KIND_SVN */
} FuEfiSignaturePrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuEfiSignature, fu_efi_signature, FU_TYPE_FIRMWARE)
#define GET_PRIVATE(o) (fu_efi_signature_get_instance_private(o))

static void
fu_efi_signature_export(FuFirmware *firmware,
			FuFirmwareExportFlags flags,
			FwupdJsonObject *json_object)
{
	FuEfiSignature *self = FU_EFI_SIGNATURE(firmware);
	FuEfiSignaturePrivate *priv = GET_PRIVATE(self);
	const gchar *kind_str = fu_efi_signature_kind_to_string(priv->kind);

	if (kind_str != NULL)
		fwupd_json_object_add_string(json_object, "kind", kind_str);
	if (priv->owner != NULL)
		fwupd_json_object_add_string(json_object, "owner", priv->owner);
	if (priv->svn_guid != NULL)
		fwupd_json_object_add_string(json_object, "svn_guid", priv->svn_guid);

	/* special case: this is *literally* a hash */
	if (priv->kind == FU_EFI_SIGNATURE_KIND_SHA256) {
		g_autoptr(GBytes) blob = fu_firmware_get_bytes(firmware, NULL);
		if (blob != NULL) {
			g_autofree gchar *str = fu_bytes_to_string(blob);
			fwupd_json_object_add_string(json_object, "checksum", str);
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
 **/
FuEfiSignature *
fu_efi_signature_new(FuEfiSignatureKind kind)
{
	g_autoptr(FuEfiSignature) self = g_object_new(FU_TYPE_EFI_SIGNATURE, NULL);
	fu_efi_signature_set_kind(self, kind);
	return g_steal_pointer(&self);
}

/**
 * fu_efi_signature_get_kind:
 * @self: A #FuEfiSignature
 *
 * Returns the signature kind.
 *
 * Returns: #FuEfiSignatureKind, e.g. %FU_EFI_SIGNATURE_KIND_SHA256
 **/
FuEfiSignatureKind
fu_efi_signature_get_kind(FuEfiSignature *self)
{
	FuEfiSignaturePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_EFI_SIGNATURE(self), FU_EFI_SIGNATURE_KIND_UNKNOWN);
	return priv->kind;
}

/* private */
void
fu_efi_signature_set_owner(FuEfiSignature *self, const gchar *owner)
{
	FuEfiSignaturePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_EFI_SIGNATURE(self));
	g_set_str(&priv->owner, owner);
}

/* private */
void
fu_efi_signature_set_kind(FuEfiSignature *self, FuEfiSignatureKind kind)
{
	FuEfiSignaturePrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_EFI_SIGNATURE(self));
	priv->kind = kind;
	if (priv->kind == FU_EFI_SIGNATURE_KIND_EXTERNAL) {
		g_autoptr(GBytes) blob = g_bytes_new_static("\x00", 1);
		fu_efi_signature_set_owner(self, FU_EFI_SIGNATURE_GUID_EXTERNAL);
		fu_firmware_set_bytes(FU_FIRMWARE(self), blob);
	}
	if (priv->kind == FU_EFI_SIGNATURE_KIND_SVN)
		fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_PAIR);
}

/**
 * fu_efi_signature_get_owner:
 * @self: A #FuEfiSignature
 *
 * Returns the GUID of the signature owner.
 *
 * Returns: GUID owner, perhaps %FU_EFI_SIGNATURE_GUID_MICROSOFT
 **/
const gchar *
fu_efi_signature_get_owner(FuEfiSignature *self)
{
	FuEfiSignaturePrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_EFI_SIGNATURE(self), NULL);
	return priv->owner;
}

/* an SVN is stored as a fake SHA256 hash with a fixed header and zero padding */
static gboolean
fu_efi_signature_parse_svn(FuEfiSignature *self, FuInputStream *stream, GError **error)
{
	FuEfiSignaturePrivate *priv = GET_PRIVATE(self);
	const gsize offset = sizeof(fwupd_guid_t);
	guint8 version_hint = 0;
	g_autofree gchar *guid_str = NULL;
	g_autoptr(FuStructEfiSignatureSvn) st = NULL;
	g_autoptr(GError) error_local = NULL;

	/* almost every entry is a real hash, so failing is expected */
	if (!fu_input_stream_read_u8(stream, offset, &version_hint, error))
		return FALSE;
	if (version_hint != FU_STRUCT_EFI_SIGNATURE_SVN_DEFAULT_VERSION)
		return TRUE;
	st = fu_struct_efi_signature_svn_parse_stream(stream, offset, &error_local);
	if (st == NULL) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_INVALID_DATA))
			return TRUE;
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	fu_efi_signature_set_kind(self, FU_EFI_SIGNATURE_KIND_SVN);
	guid_str = fwupd_guid_to_string(fu_struct_efi_signature_svn_get_guid(st),
					FWUPD_GUID_FLAG_MIXED_ENDIAN);
	g_set_str(&priv->svn_guid, guid_str);
	fu_firmware_set_version_raw(FU_FIRMWARE(self),
				    ((guint32)fu_struct_efi_signature_svn_get_major(st) << 16) |
					fu_struct_efi_signature_svn_get_minor(st));

	/* success */
	return TRUE;
}

static gboolean
fu_efi_signature_parse(FuFirmware *firmware,
		       FuInputStream *stream,
		       FuFirmwareParseFlags flags,
		       GError **error)
{
	FuEfiSignature *self = FU_EFI_SIGNATURE(firmware);
	FuEfiSignaturePrivate *priv = GET_PRIVATE(self);
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
		g_prefix_error_literal(error, "failed to read signature GUID: ");
		return FALSE;
	}
	g_free(priv->owner);
	priv->owner = fwupd_guid_to_string(&guid, FWUPD_GUID_FLAG_MIXED_ENDIAN);

	/* if data size is unspecified, we'll use the stream size */
	data = fu_input_stream_read_bytes(stream,
					  sizeof(fwupd_guid_t),
					  size - sizeof(fwupd_guid_t),
					  NULL,
					  error);
	if (data == NULL) {
		g_prefix_error_literal(error, "failed to read signature data: ");
		return FALSE;
	}
	fu_firmware_set_bytes(firmware, data);

	/* SVNs pretend to be SHA256 hashes */
	if (priv->kind == FU_EFI_SIGNATURE_KIND_SHA256 &&
	    g_bytes_get_size(data) == FU_STRUCT_EFI_SIGNATURE_SVN_SIZE) {
		if (!fu_efi_signature_parse_svn(self, stream, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

/* SVN is generated from the GUID and version */
static GBytes *
fu_efi_signature_get_svn_payload(FuEfiSignature *self, GError **error)
{
	FuEfiSignaturePrivate *priv = GET_PRIVATE(self);
	guint64 version_raw;
	fwupd_guid_t guid = {0};
	g_autoptr(FuStructEfiSignatureSvn) st = NULL;

	if (priv->svn_guid == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "SVN GUID not set");
		return NULL;
	}
	if (!fwupd_guid_from_string(priv->svn_guid, &guid, FWUPD_GUID_FLAG_MIXED_ENDIAN, error))
		return NULL;
	version_raw = fu_firmware_get_version_raw(FU_FIRMWARE(self));
	st = fu_struct_efi_signature_svn_new();
	fu_struct_efi_signature_svn_set_guid(st, &guid);
	fu_struct_efi_signature_svn_set_major(st, (version_raw >> 16) & G_MAXUINT16);
	fu_struct_efi_signature_svn_set_minor(st, version_raw & G_MAXUINT16);
	return g_bytes_new(st->buf->data, st->buf->len);
}

static GBytes *
fu_efi_signature_get_payload(FuEfiSignature *self, GError **error)
{
	FuEfiSignaturePrivate *priv = GET_PRIVATE(self);
	if (priv->kind == FU_EFI_SIGNATURE_KIND_SVN)
		return fu_efi_signature_get_svn_payload(self, error);
	return fu_firmware_get_bytes_with_patches(FU_FIRMWARE(self), error);
}

static GByteArray *
fu_efi_signature_write(FuFirmware *firmware, GError **error)
{
	FuEfiSignature *self = FU_EFI_SIGNATURE(firmware);
	FuEfiSignaturePrivate *priv = GET_PRIVATE(self);
	fwupd_guid_t owner = {0};
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GBytes) data = NULL;

	/* optional owner */
	if (priv->owner != NULL) {
		if (!fwupd_guid_from_string(priv->owner,
					    &owner,
					    FWUPD_GUID_FLAG_MIXED_ENDIAN,
					    error))
			return NULL;
	}
	g_byte_array_append(buf, owner, sizeof(owner));

	/* data */
	data = fu_efi_signature_get_payload(self, error);
	if (data == NULL)
		return NULL;
	fu_byte_array_append_bytes(buf, data);

	/* success */
	return g_steal_pointer(&buf);
}

static gboolean
fu_efi_signature_build(FuFirmware *firmware, FwupdJsonObject *json_object, GError **error)
{
	FuEfiSignature *self = FU_EFI_SIGNATURE(firmware);
	FuEfiSignaturePrivate *priv = GET_PRIVATE(self);
	const gchar *tmp;

	/* optional properties */
	if (!fwupd_json_object_get_string_optional(json_object, "kind", &tmp, error))
		return FALSE;
	if (tmp != NULL) {
		FuEfiSignatureKind kind = fu_efi_signature_kind_from_string(tmp);
		if (kind == FU_EFI_SIGNATURE_KIND_UNKNOWN) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "invalid kind: %s",
				    tmp);
			return FALSE;
		}
		fu_efi_signature_set_kind(self, kind);
	}
	if (!fwupd_json_object_get_string_optional(json_object, "owner", &tmp, error))
		return FALSE;
	if (tmp != NULL) {
		if (!fwupd_guid_from_string(tmp, NULL, FWUPD_GUID_FLAG_MIXED_ENDIAN, error)) {
			g_prefix_error(error, "failed to parse owner %s, expected GUID: ", tmp);
			return FALSE;
		}
		fu_efi_signature_set_owner(self, tmp);
	}
	if (!fwupd_json_object_get_string_optional(json_object, "svn_guid", &tmp, error))
		return FALSE;
	if (tmp != NULL) {
		if (!fwupd_guid_from_string(tmp, NULL, FWUPD_GUID_FLAG_MIXED_ENDIAN, error)) {
			g_prefix_error(error, "failed to parse svn_guid %s, expected GUID: ", tmp);
			return FALSE;
		}
		g_set_str(&priv->svn_guid, tmp);
	}
	if (!fwupd_json_object_get_string_optional(json_object, "checksum", &tmp, error))
		return FALSE;
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
	FuEfiSignaturePrivate *priv = GET_PRIVATE(self);
	g_autoptr(GBytes) data = fu_efi_signature_get_payload(self, error);
	if (data == NULL)
		return NULL;

	/* special case: this is *literally* a hash */
	if (priv->kind == FU_EFI_SIGNATURE_KIND_SHA256 && csum_kind == G_CHECKSUM_SHA256)
		return fu_bytes_to_string(data);

	/* fallback */
	return g_compute_checksum_for_bytes(csum_kind, data);
}

static gchar *
fu_efi_signature_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	FuEfiSignature *self = FU_EFI_SIGNATURE(firmware);
	FuEfiSignaturePrivate *priv = GET_PRIVATE(self);
	if (priv->kind != FU_EFI_SIGNATURE_KIND_SVN)
		return NULL;
	return fu_version_from_uint32(version_raw, FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_efi_signature_finalize(GObject *obj)
{
	FuEfiSignature *self = FU_EFI_SIGNATURE(obj);
	FuEfiSignaturePrivate *priv = GET_PRIVATE(self);
	g_free(priv->owner);
	g_free(priv->svn_guid);
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
	firmware_class->convert_version = fu_efi_signature_convert_version;
	fu_firmware_set_size_max(firmware_class, 500 * FU_KB);
}

static void
fu_efi_signature_init(FuEfiSignature *self)
{
	FuEfiSignaturePrivate *priv = GET_PRIVATE(self);
	priv->kind = FU_EFI_SIGNATURE_KIND_SHA256;
}

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
#include "fu-efi-x509-signature-private.h"
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

/**
 * fu_efi_signature_list_get_newest:
 * @self: a #FuEfiSignatureList
 *
 * Gets the deduplicated list of the newest EFI_SIGNATURE_LIST entries.
 *
 * Returns: (transfer container) (element-type FuEfiSignature): signatures
 *
 * Since: 2.0.8
 **/
GPtrArray *
fu_efi_signature_list_get_newest(FuEfiSignatureList *self)
{
	g_autoptr(GHashTable) hash = NULL;
	g_autoptr(GList) sigs_values = NULL;
	g_autoptr(GPtrArray) sigs_newest = NULL;
	g_autoptr(GPtrArray) sigs = NULL;

	g_return_val_if_fail(FU_IS_EFI_SIGNATURE_LIST(self), NULL);

	/* dedupe the certificates either by the hash or by the subject vendor+name */
	hash =
	    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_object_unref);
	sigs = fu_firmware_get_images(FU_FIRMWARE(self));
	for (guint i = 0; i < sigs->len; i++) {
		FuEfiSignature *sig = g_ptr_array_index(sigs, i);
		FuEfiSignature *sig_tmp;
		g_autofree gchar *key = NULL;

		if (fu_efi_signature_get_kind(sig) == FU_EFI_SIGNATURE_KIND_X509) {
			key = g_strdup_printf(
			    "%s:%s",
			    fu_efi_x509_signature_get_subject_vendor(FU_EFI_X509_SIGNATURE(sig)),
			    fu_efi_x509_signature_get_subject_name(FU_EFI_X509_SIGNATURE(sig)));
		} else {
			key = fu_firmware_get_checksum(FU_FIRMWARE(sig), G_CHECKSUM_SHA256, NULL);
		}
		sig_tmp = g_hash_table_lookup(hash, key);
		if (sig_tmp == NULL) {
			g_debug("adding %s", key);
			g_hash_table_insert(hash, g_steal_pointer(&key), g_object_ref(sig));
		} else if (fu_firmware_get_version_raw(FU_FIRMWARE(sig)) >
			   fu_firmware_get_version_raw(FU_FIRMWARE(sig_tmp))) {
			g_debug("replacing %s", key);
			g_hash_table_insert(hash, g_steal_pointer(&key), g_object_ref(sig));
		} else {
			g_debug("ignoring %s", key);
		}
	}

	/* add the newest of each certificate */
	sigs_newest = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	sigs_values = g_hash_table_get_values(hash);
	for (GList *l = sigs_values; l != NULL; l = l->next) {
		FuEfiX509Signature *sig = FU_EFI_X509_SIGNATURE(l->data);
		g_ptr_array_add(sigs_newest, g_object_ref(sig));
	}

	/* success */
	return g_steal_pointer(&sigs_newest);
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
		g_autoptr(FuEfiSignature) sig = NULL;

		if (sig_kind == FU_EFI_SIGNATURE_KIND_X509) {
			sig = FU_EFI_SIGNATURE(fu_efi_x509_signature_new());
		} else {
			sig = fu_efi_signature_new(sig_kind);
		}
		fu_firmware_set_size(FU_FIRMWARE(sig), size);
		if (!fu_firmware_parse_stream(FU_FIRMWARE(sig),
					      stream,
					      offset_tmp,
					      FWUPD_INSTALL_FLAG_NONE,
					      error))
			return FALSE;
		if (!fu_firmware_add_image_full(FU_FIRMWARE(self), FU_FIRMWARE(sig), error))
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

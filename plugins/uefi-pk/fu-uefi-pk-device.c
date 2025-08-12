/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-uefi-pk-device.h"

struct _FuUefiPkDevice {
	FuUefiDevice parent_instance;
	gboolean has_pk_test_key;
	gchar *key_id;
};

G_DEFINE_TYPE(FuUefiPkDevice, fu_uefi_pk_device, FU_TYPE_UEFI_DEVICE)

#define FU_UEFI_PK_DEVICE_DEFAULT_REQUIRED_FREE (8 * 1024) /* bytes */

static void
fu_uefi_pk_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuUefiPkDevice *self = FU_UEFI_PK_DEVICE(device);
	fwupd_codec_string_append_bool(str, idt, "HasPkTestKey", self->has_pk_test_key);
}

#define FU_UEFI_PK_CHECKSUM_AMI_TEST_KEY "a773113bafaf5129aa83fd0912e95da4fa555f91"

static gboolean
fu_uefi_pk_device_check(FuUefiPkDevice *self, const gchar *str, GError **error)
{
	const gchar *needles[] = {
	    "DO NOT TRUST",
	    "DO NOT SHIP",
	};

	/* look for things that should not exist */
	for (guint i = 0; i < G_N_ELEMENTS(needles); i++) {
		if (g_strstr_len(str, -1, needles[i]) != NULL) {
			g_info("got %s, marking unsafe", str);
			self->has_pk_test_key = TRUE;
			break;
		}
	}

	/* success */
	return TRUE;
}

const gchar *
fu_uefi_pk_device_get_key_id(FuUefiPkDevice *self)
{
	g_return_val_if_fail(FU_IS_UEFI_PK_DEVICE(self), NULL);
	return self->key_id;
}

static void
fu_uefi_pk_device_set_key_id(FuUefiPkDevice *self, const gchar *key_id)
{
	g_free(self->key_id);
	self->key_id = g_strdup(key_id);
}

static gboolean
fu_uefi_pk_device_parse_certificate(FuUefiPkDevice *self, FuEfiX509Signature *sig, GError **error)
{
	const gchar *subject_name = fu_efi_x509_signature_get_subject_name(sig);
	const gchar *subject_vendor = fu_efi_x509_signature_get_subject_vendor(sig);

	/* look in issuer and subject */
	if (fu_efi_x509_signature_get_issuer(sig) != NULL) {
		if (!fu_uefi_pk_device_check(self, fu_efi_x509_signature_get_issuer(sig), error))
			return FALSE;
	}
	if (fu_efi_x509_signature_get_subject(sig) != NULL) {
		if (!fu_uefi_pk_device_check(self, fu_efi_x509_signature_get_subject(sig), error))
			return FALSE;
	}

	/* the O= key may not exist */
	fu_device_add_instance_strsafe(FU_DEVICE(self), "VENDOR", subject_vendor);
	fu_device_add_instance_strsafe(FU_DEVICE(self), "NAME", subject_name);
	fu_device_build_instance_id(FU_DEVICE(self), NULL, "UEFI", "VENDOR", "NAME", NULL);
	fu_device_set_name(FU_DEVICE(self), subject_name != NULL ? subject_name : "Unknown");
	fu_device_set_vendor(FU_DEVICE(self), subject_vendor != NULL ? subject_vendor : "Unknown");
	fu_device_set_version_raw(FU_DEVICE(self), fu_firmware_get_version_raw(FU_FIRMWARE(sig)));
	fu_uefi_pk_device_set_key_id(self, fu_firmware_get_id(FU_FIRMWARE(sig)));

	/* success, certificate was parsed correctly */
	fu_device_add_instance_strup(FU_DEVICE(self), "CRT", self->key_id);
	return fu_device_build_instance_id(FU_DEVICE(self), error, "UEFI", "CRT", NULL);
}

static gboolean
fu_uefi_pk_device_probe(FuDevice *device, GError **error)
{
	FuUefiPkDevice *self = FU_UEFI_PK_DEVICE(device);
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(FuFirmware) pk = NULL;
	g_autoptr(FuProgress) progress = fu_progress_new(G_STRLOC);
	g_autoptr(GPtrArray) sigs = NULL;

	/* FuUefiDevice->probe */
	if (!FU_DEVICE_CLASS(fu_uefi_pk_device_parent_class)->probe(device, error))
		return FALSE;

	pk = fu_device_read_firmware(device,
				     progress,
				     FU_FIRMWARE_PARSE_FLAG_IGNORE_CHECKSUM,
				     error);
	if (pk == NULL) {
		g_prefix_error_literal(error, "failed to parse PK: ");
		return FALSE;
	}

	/* by checksum */
	img = fu_firmware_get_image_by_checksum(pk, FU_UEFI_PK_CHECKSUM_AMI_TEST_KEY, NULL);
	if (img != NULL)
		self->has_pk_test_key = TRUE;

	/* by text */
	sigs = fu_firmware_get_images(pk);
	for (guint i = 0; i < sigs->len; i++) {
		FuEfiSignature *sig = g_ptr_array_index(sigs, i);
		if (fu_efi_signature_get_kind(sig) != FU_EFI_SIGNATURE_KIND_X509)
			continue;
		if (!fu_uefi_pk_device_parse_certificate(self, FU_EFI_X509_SIGNATURE(sig), error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_uefi_pk_device_add_security_attrs(FuDevice *device, FuSecurityAttrs *attrs)
{
	FuUefiPkDevice *self = FU_UEFI_PK_DEVICE(device);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fu_device_security_attr_new(device, FWUPD_SECURITY_ATTR_ID_UEFI_PK);
	fwupd_security_attr_set_result_success(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
	fu_security_attrs_append(attrs, attr);

	/* test key is not secure */
	if (self->has_pk_test_key) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONFIG_FW);
		fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_ACTION_CONTACT_OEM);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
}

static gchar *
fu_uefi_pk_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint64(version_raw, fu_device_get_version_format(device));
}

static void
fu_uefi_pk_device_init(FuUefiPkDevice *self)
{
	fu_device_set_physical_id(FU_DEVICE(self), "pk");
	fu_device_set_summary(FU_DEVICE(self), "UEFI Platform Key");
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_HOST_FIRMWARE_CHILD);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_APPLICATION_CERTIFICATE);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_EFI_SIGNATURE_LIST);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_NUMBER);
	fu_device_set_required_free(FU_DEVICE(self), FU_UEFI_PK_DEVICE_DEFAULT_REQUIRED_FREE);
}

static void
fu_uefi_pk_device_finalize(GObject *object)
{
	FuUefiPkDevice *self = FU_UEFI_PK_DEVICE(object);
	g_free(self->key_id);
	G_OBJECT_CLASS(fu_uefi_pk_device_parent_class)->finalize(object);
}

static void
fu_uefi_pk_device_class_init(FuUefiPkDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_uefi_pk_device_finalize;
	device_class->to_string = fu_uefi_pk_device_to_string;
	device_class->add_security_attrs = fu_uefi_pk_device_add_security_attrs;
	device_class->probe = fu_uefi_pk_device_probe;
	device_class->convert_version = fu_uefi_pk_device_convert_version;
}

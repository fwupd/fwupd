/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gnutls/abstract.h>
#include <gnutls/crypto.h>

#include "fu-uefi-pk-device.h"

struct _FuUefiPkDevice {
	FuDevice parent_instance;
	gboolean has_pk_test_key;
};

G_DEFINE_TYPE(FuUefiPkDevice, fu_uefi_pk_device, FU_TYPE_DEVICE)

static void
fu_uefi_pk_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuUefiPkDevice *self = FU_UEFI_PK_DEVICE(device);
	fu_string_append_kb(str, idt, "HasPkTestKey", self->has_pk_test_key);
}

#define FU_UEFI_PK_CHECKSUM_AMI_TEST_KEY "a773113bafaf5129aa83fd0912e95da4fa555f91"

static void
_gnutls_datum_deinit(gnutls_datum_t *d)
{
	gnutls_free(d->data);
	gnutls_free(d);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(gnutls_datum_t, _gnutls_datum_deinit)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_x509_crt_t, gnutls_x509_crt_deinit, NULL)
#pragma clang diagnostic pop

static gboolean
fu_uefi_pk_device_parse_buf(FuUefiPkDevice *self, const gchar *buf, gsize bufsz, GError **error)
{
	const gchar *needles[] = {
	    "DO NOT TRUST",
	    "DO NOT SHIP",
	    NULL,
	};
	g_auto(GStrv) infos = NULL;

	/* look for things that should not exist */
	for (guint i = 0; needles[i] != NULL; i++) {
		if (g_strstr_len(buf, bufsz, needles[i]) != NULL) {
			g_info("got %s, marking unsafe", buf);
			self->has_pk_test_key = TRUE;
			break;
		}
	}

	/* extract the info from C=JP,ST=KN,L=YK,O=Lenovo Ltd.,CN=Lenovo Ltd. PK CA 2012 */
	infos = fu_strsplit(buf, bufsz, ",", -1);
	for (guint i = 0; infos[i] != NULL; i++) {
		if (fu_device_get_vendor(FU_DEVICE(self)) == NULL &&
		    g_str_has_prefix(infos[i], "O=")) {
			fu_device_set_vendor(FU_DEVICE(self), infos[i] + 2);
		}
		if (fu_device_get_summary(FU_DEVICE(self)) == NULL &&
		    g_str_has_prefix(infos[i], "CN=")) {
			fu_device_set_summary(FU_DEVICE(self), infos[i] + 3);
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_uefi_pk_device_parse_signature(FuUefiPkDevice *self, FuEfiSignature *sig, GError **error)
{
	gchar buf[1024] = {'\0'};
	guchar key_id[20] = {'\0'};
	gsize key_idsz = sizeof(key_id);
	gnutls_datum_t d = {0};
	gnutls_x509_dn_t dn = {0x0};
	gsize bufsz = sizeof(buf);
	int rc;
	g_autofree gchar *key_idstr = NULL;
	g_auto(gnutls_x509_crt_t) crt = NULL;
	g_autoptr(gnutls_datum_t) subject = NULL;
	g_autoptr(GBytes) blob = NULL;

	/* create certificate */
	rc = gnutls_x509_crt_init(&crt);
	if (rc < 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "crt_init: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
		return FALSE;
	}

	/* parse certificate */
	blob = fu_firmware_get_bytes(FU_FIRMWARE(sig), error);
	if (blob == NULL)
		return FALSE;
	d.size = g_bytes_get_size(blob);
	d.data = (unsigned char *)g_bytes_get_data(blob, NULL);
	rc = gnutls_x509_crt_import(crt, &d, GNUTLS_X509_FMT_DER);
	if (rc < 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "crt_import: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
		return FALSE;
	}

	/* look in issuer */
	if (gnutls_x509_crt_get_issuer_dn(crt, buf, &bufsz) == GNUTLS_E_SUCCESS) {
		g_debug("PK issuer: %s", buf);
		if (!fu_uefi_pk_device_parse_buf(self, buf, bufsz, error))
			return FALSE;
	}

	/* look in subject */
	subject = (gnutls_datum_t *)gnutls_malloc(sizeof(gnutls_datum_t));
	if (gnutls_x509_crt_get_subject(crt, &dn) == GNUTLS_E_SUCCESS) {
		gnutls_x509_dn_get_str(dn, subject);
		g_debug("PK subject: %s", subject->data);
		if (!fu_uefi_pk_device_parse_buf(self,
						 (const gchar *)subject->data,
						 subject->size,
						 error))
			return FALSE;
	}

	/* key ID */
	rc = gnutls_x509_crt_get_key_id(crt, 0, key_id, &key_idsz);
	if (rc < 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "failed to get key ID: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
		return FALSE;
	}
	key_idstr = g_compute_checksum_for_data(G_CHECKSUM_SHA1, key_id, key_idsz);
	if (key_idstr == NULL) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "failed to calculate key ID for 0x%x bytes",
			    (guint)key_idsz);
		return FALSE;
	}
	fu_device_add_instance_strup(FU_DEVICE(self), "CRT", key_idstr);

	/* success, certificate was parsed correctly */
	return fu_device_build_instance_id(FU_DEVICE(self), error, "UEFI", "CRT", NULL);
}

static gboolean
fu_uefi_pk_device_probe(FuDevice *device, GError **error)
{
	FuUefiPkDevice *self = FU_UEFI_PK_DEVICE(device);
	g_autoptr(FuFirmware) img = NULL;
	g_autoptr(FuFirmware) pk = fu_efi_signature_list_new();
	g_autoptr(GBytes) pk_blob = NULL;
	g_autoptr(GPtrArray) sigs = NULL;

	pk_blob = fu_efivar_get_data_bytes(FU_EFIVAR_GUID_EFI_GLOBAL, "PK", NULL, error);
	if (pk_blob == NULL)
		return FALSE;
	if (!fu_firmware_parse(pk, pk_blob, FU_FIRMWARE_FLAG_NONE, error)) {
		g_prefix_error(error, "failed to parse PK: ");
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
		if (!fu_uefi_pk_device_parse_signature(self, sig, error))
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
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
}

static void
fu_uefi_pk_device_init(FuUefiPkDevice *self)
{
	fu_device_set_physical_id(FU_DEVICE(self), "pk");
	fu_device_set_name(FU_DEVICE(self), "UEFI Platform Key");
	fu_device_add_parent_guid(FU_DEVICE(self), "main-system-firmware");
}

static void
fu_uefi_pk_device_class_init(FuUefiPkDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_uefi_pk_device_to_string;
	klass_device->add_security_attrs = fu_uefi_pk_device_add_security_attrs;
	klass_device->probe = fu_uefi_pk_device_probe;
}

FuUefiPkDevice *
fu_uefi_pk_device_new(FuContext *ctx)
{
	return g_object_new(FU_TYPE_UEFI_PK_DEVICE, "context", ctx, NULL);
}

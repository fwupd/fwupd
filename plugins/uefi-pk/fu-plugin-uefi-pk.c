/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <gnutls/abstract.h>
#include <gnutls/crypto.h>

struct FuPluginData {
	gboolean has_pk_test_key;
};

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
fu_plugin_uefi_pk_parse_buf(FuPlugin *plugin, const gchar *buf, gsize bufsz, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	const gchar *needles[] = {
	    "DO NOT TRUST",
	    "DO NOT SHIP",
	    NULL,
	};
	for (guint i = 0; needles[i] != NULL; i++) {
		if (g_strstr_len(buf, bufsz, needles[i]) != NULL) {
			g_debug("got %s, marking unsafe", buf);
			priv->has_pk_test_key = TRUE;
			break;
		}
	}
	return TRUE;
}

static gboolean
fu_plugin_uefi_pk_parse_signature(FuPlugin *plugin, FuEfiSignature *sig, GError **error)
{
	gchar buf[1024] = {'\0'};
	gnutls_datum_t d = {0};
	gnutls_x509_dn_t dn = {0x0};
	gsize bufsz = sizeof(buf);
	int rc;
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
		if (g_getenv("FWUPD_UEFI_PK_VERBOSE") != NULL)
			g_debug("PK issuer: %s", buf);
		if (!fu_plugin_uefi_pk_parse_buf(plugin, buf, bufsz, error))
			return FALSE;
	}

	/* look in subject */
	subject = (gnutls_datum_t *)gnutls_malloc(sizeof(gnutls_datum_t));
	if (gnutls_x509_crt_get_subject(crt, &dn) == GNUTLS_E_SUCCESS) {
		gnutls_x509_dn_get_str(dn, subject);
		if (g_getenv("FWUPD_UEFI_PK_VERBOSE") != NULL)
			g_debug("PK subject: %s", subject->data);
		if (!fu_plugin_uefi_pk_parse_buf(plugin,
						 (const gchar *)subject->data,
						 subject->size,
						 error))
			return FALSE;
	}

	/* success, certificate was parsed correctly */
	return TRUE;
}

static gboolean
fu_plugin_uefi_pk_coldplug(FuPlugin *plugin, FuProgress *progress, GError **error)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
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
	if (img != NULL) {
		g_debug("detected AMI test certificate");
		priv->has_pk_test_key = TRUE;
	}

	/* by text */
	sigs = fu_firmware_get_images(pk);
	for (guint i = 0; i < sigs->len; i++) {
		FuEfiSignature *sig = g_ptr_array_index(sigs, i);
		if (!fu_plugin_uefi_pk_parse_signature(plugin, sig, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_plugin_uefi_pk_init(FuPlugin *plugin)
{
	fu_plugin_alloc_data(plugin, sizeof(FuPluginData));
}

static void
fu_plugin_uefi_pk_device_registered(FuPlugin *plugin, FuDevice *device)
{
	if (fu_device_has_instance_id(device, "main-system-firmware"))
		fu_plugin_cache_add(plugin, "main-system-firmware", device);
}

static void
fu_plugin_uefi_pk_add_security_attrs(FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data(plugin);
	FuDevice *msf_device = fu_plugin_cache_lookup(plugin, "main-system-firmware");
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new(FWUPD_SECURITY_ATTR_ID_UEFI_PK);
	fwupd_security_attr_set_level(attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_set_plugin(attr, fu_plugin_get_name(plugin));
	if (msf_device != NULL)
		fwupd_security_attr_add_guids(attr, fu_device_get_guids(msf_device));
	fu_security_attrs_append(attrs, attr);

	/* not enabled or test key is not secure */
	if (priv == NULL || priv->has_pk_test_key) {
		fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag(attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result(attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
}

void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs)
{
	vfuncs->build_hash = FU_BUILD_HASH;
	vfuncs->init = fu_plugin_uefi_pk_init;
	vfuncs->add_security_attrs = fu_plugin_uefi_pk_add_security_attrs;
	vfuncs->device_registered = fu_plugin_uefi_pk_device_registered;
	vfuncs->coldplug = fu_plugin_uefi_pk_coldplug;
}

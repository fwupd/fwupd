/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gnutls/crypto.h>
#include <gnutls/abstract.h>

#include "fu-plugin-vfuncs.h"
#include "fu-efivar.h"
#include "fu-hash.h"
#include "fu-efi-signature-common.h"
#include "fu-efi-signature-parser.h"
#include "fu-uefi-dbx-common.h"
#include "fu-uefi-dbx-device.h"

struct FuPluginData {
	gboolean		 has_pk_test_key;
};

#define FU_UEFI_PK_CHECKSUM_AMI_TEST_KEY	"a773113bafaf5129aa83fd0912e95da4fa555f91"

void
fu_plugin_init (FuPlugin *plugin)
{
	fu_plugin_alloc_data (plugin, sizeof (FuPluginData));
	fu_plugin_set_build_hash (plugin, FU_BUILD_HASH);
	fu_plugin_add_rule (plugin, FU_PLUGIN_RULE_METADATA_SOURCE, "uefi");
}

static void
_gnutls_datum_deinit (gnutls_datum_t *d)
{
	gnutls_free (d->data);
	gnutls_free (d);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(gnutls_datum_t, _gnutls_datum_deinit)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_x509_crt_t, gnutls_x509_crt_deinit, NULL)
#pragma clang diagnostic pop

static gboolean
fu_plugin_uefi_security_parse_buf (FuPlugin *plugin,
				   const gchar *buf,
				   gsize bufsz,
				   GError **error)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	const gchar *needles[] = {
		"DO NOT TRUST",
		"DO NOT SHIP",
		NULL,
	};
	for (guint i = 0; needles[i] != NULL; i++) {
		if (g_strstr_len (buf, bufsz, needles[i]) != NULL) {
			g_warning ("got %s, marking unsafe", buf);
			priv->has_pk_test_key = TRUE;
			break;
		}
	}
	return TRUE;
}

static gboolean
fu_plugin_uefi_security_parse_blob (FuPlugin *plugin,
				    GBytes *blob,
				    GError **error)
{
	gchar buf[1024] = { '\0' };
	gnutls_datum_t d = { 0 };
	gnutls_x509_dn_t dn = { 0x0 };
	gsize bufsz = sizeof(buf);
	int rc;
	g_auto(gnutls_x509_crt_t) crt = NULL;
	g_autoptr(gnutls_datum_t) subject = NULL;

	/* create certificate */
	rc = gnutls_x509_crt_init (&crt);
	if (rc < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "crt_init: %s [%i]",
			     gnutls_strerror (rc), rc);
		return FALSE;
	}

	/* parse certificate */
	d.size = g_bytes_get_size (blob);
	d.data = (unsigned char *) g_bytes_get_data (blob, NULL);
	rc = gnutls_x509_crt_import (crt, &d, GNUTLS_X509_FMT_DER);
	if (rc < 0) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "crt_import: %s [%i]",
			     gnutls_strerror (rc), rc);
		return FALSE;
	}

	/* look in issuer */
	if (gnutls_x509_crt_get_issuer_dn (crt, buf, &bufsz) == GNUTLS_E_SUCCESS) {
		if (g_getenv ("FWUPD_UEFI_PK_VERBOSE") != NULL)
			g_debug ("PK issuer: %s", buf);
		if (!fu_plugin_uefi_security_parse_buf (plugin,
						  buf, bufsz,
						  error))
			return FALSE;
	}

	/* look in subject */
	subject = (gnutls_datum_t *) gnutls_malloc (sizeof (gnutls_datum_t));
	if (gnutls_x509_crt_get_subject (crt, &dn) == GNUTLS_E_SUCCESS) {
		gnutls_x509_dn_get_str (dn, subject);
		if (g_getenv ("FWUPD_UEFI_PK_VERBOSE") != NULL)
			g_debug ("PK subject: %s", subject->data);
		if (!fu_plugin_uefi_security_parse_buf (plugin,
						  (const gchar *) subject->data,
						  subject->size,
						  error))
			return FALSE;
	}

	/* success, certificate was parsed correctly */
	return TRUE;
}

static gboolean
fu_plugin_uefi_security_parse_siglist (FuPlugin *plugin,
				       FuEfiSignatureList *siglist,
				       GError **error)
{
	GPtrArray *sigs = fu_efi_signature_list_get_all (siglist);
	FuPluginData *priv = fu_plugin_get_data (plugin);
	for (guint i = 0; i < sigs->len; i++) {
		FuEfiSignature *sig = g_ptr_array_index (sigs, i);
		GBytes *blob = fu_efi_signature_get_data (sig);
		g_debug ("owner: %s, checksum: %s",
			 fu_efi_signature_get_owner (sig),
			 fu_efi_signature_get_checksum (sig));
		if (g_strcmp0 (fu_efi_signature_get_checksum (sig),
			       FU_UEFI_PK_CHECKSUM_AMI_TEST_KEY) == 0) {
			g_debug ("detected AMI test certificate");
			priv->has_pk_test_key = TRUE;
		} else if (!fu_plugin_uefi_security_parse_blob (plugin, blob, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_coldplug (FuPlugin *plugin, GError **error)
{
	gsize bufsz = 0;
	g_autofree guint8 *buf = NULL;
	g_autoptr(GPtrArray) siglists = NULL;
	g_autoptr(FuUefiDbxDevice) device = NULL;

	/* PK support */
	if (!fu_efivar_get_data (FU_EFIVAR_GUID_EFI_GLOBAL, "PK",
				 &buf, &bufsz, NULL, error)) {
		g_prefix_error (error, "failed to read PK: ");
		return FALSE;
	}
	siglists = fu_efi_signature_parser_new (buf, bufsz,
						FU_EFI_SIGNATURE_PARSER_FLAGS_NONE,
						error);
	if (siglists == NULL) {
		g_prefix_error (error, "failed to parse PK: ");
		return FALSE;
	}
	for (guint i = 0; i < siglists->len; i++) {
		FuEfiSignatureList *siglist = g_ptr_array_index (siglists, i);
		if (fu_efi_signature_list_get_kind (siglist) == FU_EFI_SIGNATURE_KIND_X509)
			if (!fu_plugin_uefi_security_parse_siglist (plugin, siglist, error))
				return FALSE;
	}

	/* dbx support */
	device = fu_uefi_dbx_device_new ();
	if (!fu_device_probe (FU_DEVICE (device), error))
		return FALSE;
	if (!fu_device_setup (FU_DEVICE (device), error))
		return FALSE;
	fu_plugin_device_add (plugin, FU_DEVICE (device));

	return TRUE;
}

void
fu_plugin_add_security_attrs (FuPlugin *plugin, FuSecurityAttrs *attrs)
{
	FuPluginData *priv = fu_plugin_get_data (plugin);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	/* create attr */
	attr = fwupd_security_attr_new (FWUPD_SECURITY_ATTR_ID_UEFI_PK);
	fwupd_security_attr_set_level (attr, FWUPD_SECURITY_ATTR_LEVEL_CRITICAL);
	fwupd_security_attr_set_plugin (attr, fu_plugin_get_name (plugin));
	fu_security_attrs_append (attrs, attr);

	if (priv->has_pk_test_key) {
		fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_NOT_VALID);
		return;
	}

	/* success */
	fwupd_security_attr_add_flag (attr, FWUPD_SECURITY_ATTR_FLAG_SUCCESS);
	fwupd_security_attr_set_result (attr, FWUPD_SECURITY_ATTR_RESULT_VALID);
}

/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#ifdef HAVE_GNUTLS
#include <gnutls/abstract.h>
#include <gnutls/crypto.h>
#endif

#include "fu-common.h"
#include "fu-efi-signature-private.h"
#include "fu-efi-x509-signature-private.h"
#include "fu-string.h"
#include "fu-version-common.h"

#ifdef HAVE_GNUTLS
static void
fu_efi_x509_signature_gnutls_datum_deinit(gnutls_datum_t *d)
{
	gnutls_free(d->data);
	gnutls_free(d);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
G_DEFINE_AUTOPTR_CLEANUP_FUNC(gnutls_datum_t, fu_efi_x509_signature_gnutls_datum_deinit)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gnutls_x509_crt_t, gnutls_x509_crt_deinit, NULL)
#pragma clang diagnostic pop
#endif

/**
 * FuEfiX509Signature:
 *
 * A X.509 certificate as found in an `EFI_SIGNATURE_LIST`.
 *
 * See also: [class@FuFirmware]
 */

struct _FuEfiX509Signature {
	FuEfiSignature parent_instance;
	gchar *issuer;
	gchar *subject;
	gchar *subject_name;
	gchar *subject_vendor;
};

G_DEFINE_TYPE(FuEfiX509Signature, fu_efi_x509_signature, FU_TYPE_EFI_SIGNATURE)

static void
fu_efi_x509_signature_export(FuFirmware *firmware, FuFirmwareExportFlags flags, XbBuilderNode *bn)
{
	FuEfiX509Signature *self = FU_EFI_X509_SIGNATURE(firmware);
	fu_xmlb_builder_insert_kv(bn, "issuer", self->issuer);
	fu_xmlb_builder_insert_kv(bn, "subject", self->subject);
	fu_xmlb_builder_insert_kv(bn, "subject_name", self->subject_name);
	fu_xmlb_builder_insert_kv(bn, "subject_vendor", self->subject_vendor);
}

/**
 * fu_efi_x509_signature_get_issuer:
 * @self: A #FuEfiX509Signature
 *
 * Returns the certificate issuer.
 *
 * Returns: string, or %NULL for unset
 *
 * Since: 2.0.8
 **/
const gchar *
fu_efi_x509_signature_get_issuer(FuEfiX509Signature *self)
{
	g_return_val_if_fail(FU_IS_EFI_X509_SIGNATURE(self), NULL);
	return self->issuer;
}

/* private */
void
fu_efi_x509_signature_set_issuer(FuEfiX509Signature *self, const gchar *issuer)
{
	g_return_if_fail(FU_IS_EFI_X509_SIGNATURE(self));
	if (g_strcmp0(issuer, self->issuer) == 0)
		return;
	g_free(self->issuer);
	self->issuer = g_strdup(issuer);
}

static gchar *
fu_efi_x509_signature_normalize_vendor(const gchar *text)
{
	GString *str = g_string_new(text);
	struct {
		const gchar *search;
		const gchar *replace;
	} dmi_map[] = {
	    {"ASUSTeK MotherBoard", "ASUSTeK"},
	    {"ASUSTeK Notebook", "ASUSTeK"},
	    {"Canonical Ltd.", "Canonical"},
	    {"Dell Inc.", "Dell"},
	    {"Hughski Ltd.", "Hughski"},
	    {"Lenovo(Beijing) Ltd", "Lenovo"},
	    {"Lenovo Ltd.", "Lenovo"},
	    {"LG Electronics inc.", "LG"},
	    {"Microsoft Corporation", "Microsoft"},
	    {"KEK 2K CA", "KEK CA"},
	    {"KEK 3K CA", "KEK CA"},
	};

	/* make the certificate match DMI for LVFS permissions */
	for (guint i = 0; i < G_N_ELEMENTS(dmi_map); i++)
		g_string_replace(str, dmi_map[i].search, dmi_map[i].replace, 0);
	return g_string_free(str, FALSE);
}

static void
fu_efi_x509_signature_set_subject_vendor(FuEfiX509Signature *self, const gchar *vendor)
{
	self->subject_vendor = fu_efi_x509_signature_normalize_vendor(vendor);
}

static void
fu_efi_x509_signature_set_subject_name(FuEfiX509Signature *self, const gchar *name)
{
	g_autoptr(GString) str = g_string_new(name);

	/* remove any year suffix */
	if (str->len >= 5) {
		guint64 version_raw = 0;
		if (fu_strtoull(str->str + str->len - 4,
				&version_raw,
				1982,
				2099,
				FU_INTEGER_BASE_10,
				NULL)) {
			g_string_truncate(str, str->len - 5);
			fu_firmware_set_version_raw(FU_FIRMWARE(self), version_raw);
		}
	}
	self->subject_name = fu_efi_x509_signature_normalize_vendor(str->str);
}

/* private */
void
fu_efi_x509_signature_set_subject(FuEfiX509Signature *self, const gchar *subject)
{
	g_return_if_fail(FU_IS_EFI_X509_SIGNATURE(self));
	if (g_strcmp0(subject, self->subject) == 0)
		return;
	g_free(self->subject);
	self->subject = g_strdup(subject);

	/* parse out two keys things we need */
	if (subject != NULL) {
		g_auto(GStrv) attrs = g_strsplit(subject, ",", -1);
		for (guint i = 0; attrs[i] != NULL; i++) {
			if (g_str_has_prefix(attrs[i], "O=")) {
				fu_efi_x509_signature_set_subject_vendor(self, attrs[i] + 2);
				continue;
			}
			if (g_str_has_prefix(attrs[i], "CN=")) {
				fu_efi_x509_signature_set_subject_name(self, attrs[i] + 3);
				continue;
			}
		}
	}
}

/**
 * fu_efi_x509_signature_get_subject:
 * @self: A #FuEfiX509Signature
 *
 * Returns the certificate subject.
 *
 * Returns: string, or %NULL for unset
 *
 * Since: 2.0.8
 **/
const gchar *
fu_efi_x509_signature_get_subject(FuEfiX509Signature *self)
{
	g_return_val_if_fail(FU_IS_EFI_X509_SIGNATURE(self), NULL);
	return self->subject;
}

/**
 * fu_efi_x509_signature_get_subject_name:
 * @self: A #FuEfiX509Signature
 *
 * Returns the certificate subject name, with any suffixed version removed.
 *
 * Returns: string, or %NULL for unset
 *
 * Since: 2.0.8
 **/
const gchar *
fu_efi_x509_signature_get_subject_name(FuEfiX509Signature *self)
{
	g_return_val_if_fail(FU_IS_EFI_X509_SIGNATURE(self), NULL);
	return self->subject_name;
}

/**
 * fu_efi_x509_signature_get_subject_vendor:
 * @self: A #FuEfiX509Signature
 *
 * Returns the certificate subject name, with any suffixed version removed.
 *
 * Returns: string, or %NULL for unset
 *
 * Since: 2.0.8
 **/
const gchar *
fu_efi_x509_signature_get_subject_vendor(FuEfiX509Signature *self)
{
	g_return_val_if_fail(FU_IS_EFI_X509_SIGNATURE(self), NULL);
	return self->subject_vendor;
}

static gboolean
fu_efi_x509_signature_parse(FuFirmware *firmware,
			    GInputStream *stream,
			    FwupdInstallFlags flags,
			    GError **error)
{
#ifdef HAVE_GNUTLS
	FuEfiX509Signature *self = FU_EFI_X509_SIGNATURE(firmware);
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

	/* set bytes */
	if (!FU_FIRMWARE_CLASS(fu_efi_x509_signature_parent_class)
		 ->parse(firmware, stream, flags, error))
		return FALSE;

	/* parse certificate */
	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;
	d.size = g_bytes_get_size(blob);
	d.data = (unsigned char *)g_bytes_get_data(blob, NULL);
	rc = gnutls_x509_crt_init(&crt);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "crt_init: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
		return FALSE;
	}
	rc = gnutls_x509_crt_import(crt, &d, GNUTLS_X509_FMT_DER);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "crt_import: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
		return FALSE;
	}

	/* issuer */
	if (gnutls_x509_crt_get_issuer_dn(crt, buf, &bufsz) == GNUTLS_E_SUCCESS) {
		g_autofree gchar *str = fu_strsafe((const gchar *)buf, bufsz);
		fu_efi_x509_signature_set_issuer(self, str);
	}

	/* subject */
	subject = (gnutls_datum_t *)gnutls_malloc(sizeof(gnutls_datum_t));
	if (gnutls_x509_crt_get_subject(crt, &dn) == GNUTLS_E_SUCCESS) {
		g_autofree gchar *str = NULL;
		gnutls_x509_dn_get_str(dn, subject);
		str = fu_strsafe((const gchar *)subject->data, subject->size);
		fu_efi_x509_signature_set_subject(self, str);
	}

	/* key ID */
	rc = gnutls_x509_crt_get_key_id(crt, 0, key_id, &key_idsz);
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to get key ID: %s [%i]",
			    gnutls_strerror(rc),
			    rc);
		return FALSE;
	}
	key_idstr = g_compute_checksum_for_data(G_CHECKSUM_SHA1, key_id, key_idsz);
	if (key_idstr == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to calculate key ID for 0x%x bytes",
			    (guint)key_idsz);
		return FALSE;
	}
	fu_firmware_set_id(firmware, key_idstr);

	/* success */
	return TRUE;
#else
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no GnuTLS support");
	return FALSE;
#endif
}

static gchar *
fu_efi_x509_signature_convert_version(FuFirmware *firmware, guint64 version_raw)
{
	return fu_version_from_uint64(version_raw, fu_firmware_get_version_format(firmware));
}

static void
fu_efi_x509_signature_init(FuEfiX509Signature *self)
{
	fu_efi_signature_set_kind(FU_EFI_SIGNATURE(self), FU_EFI_SIGNATURE_KIND_X509);
	fu_firmware_set_version_format(FU_FIRMWARE(self), FWUPD_VERSION_FORMAT_NUMBER);
}

static void
fu_efi_x509_signature_finalize(GObject *obj)
{
	FuEfiX509Signature *self = FU_EFI_X509_SIGNATURE(obj);
	g_free(self->issuer);
	g_free(self->subject);
	g_free(self->subject_name);
	g_free(self->subject_vendor);
	G_OBJECT_CLASS(fu_efi_x509_signature_parent_class)->finalize(obj);
}

static void
fu_efi_x509_signature_class_init(FuEfiX509SignatureClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuFirmwareClass *firmware_class = FU_FIRMWARE_CLASS(klass);
	object_class->finalize = fu_efi_x509_signature_finalize;
	firmware_class->export = fu_efi_x509_signature_export;
	firmware_class->parse = fu_efi_x509_signature_parse;
	firmware_class->convert_version = fu_efi_x509_signature_convert_version;
}

/**
 * fu_efi_x509_signature_new:
 *
 * Creates a new #FuEfiX509Signature.
 *
 * Returns: (transfer full): object
 *
 * Since: 2.0.8
 **/
FuEfiX509Signature *
fu_efi_x509_signature_new(void)
{
	return g_object_new(FU_TYPE_EFI_X509_SIGNATURE, NULL);
}

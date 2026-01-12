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
#include "fu-x509-certificate.h"

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
gchar *
fu_efi_x509_signature_build_dedupe_key(FuEfiX509Signature *self)
{
	g_return_val_if_fail(FU_IS_EFI_X509_SIGNATURE(self), NULL);

	/* in 2023 Microsoft renamed "Microsoft Windows Production PCA" -> "Windows UEFI CA" */
	if (g_strcmp0(self->subject_vendor, "Microsoft") == 0 &&
	    g_strcmp0(self->subject_name, "Microsoft Windows Production PCA") == 0) {
		return g_strdup("Microsoft:Windows UEFI CA");
	}
	return g_strdup_printf("%s:%s", self->subject_vendor, self->subject_name);
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
			    FuFirmwareParseFlags flags,
			    GError **error)
{
	FuEfiX509Signature *self = FU_EFI_X509_SIGNATURE(firmware);
	g_autoptr(FuX509Certificate) crt = fu_x509_certificate_new();
	g_autoptr(GBytes) blob = NULL;

	/* set bytes */
	if (!FU_FIRMWARE_CLASS(fu_efi_x509_signature_parent_class)
		 ->parse(firmware, stream, flags, error))
		return FALSE;

	/* parse certificate */
	blob = fu_firmware_get_bytes(firmware, error);
	if (blob == NULL)
		return FALSE;
	if (!fu_firmware_parse_bytes(FU_FIRMWARE(crt), blob, 0x0, flags, error))
		return FALSE;
	fu_firmware_set_id(firmware, fu_firmware_get_id(FU_FIRMWARE(crt)));
	fu_efi_x509_signature_set_issuer(self, fu_x509_certificate_get_issuer(crt));
	fu_efi_x509_signature_set_subject(self, fu_x509_certificate_get_subject(crt));

	/* no year in the subject, fall back */
	if (fu_firmware_get_version_raw(FU_FIRMWARE(self)) == 0) {
		g_autoptr(GDateTime) dt = fu_x509_certificate_get_activation_time(crt);
		if (dt != NULL) {
			g_debug("falling back to activation time %u",
				(guint)g_date_time_get_year(dt));
			fu_firmware_set_version_raw(FU_FIRMWARE(self), g_date_time_get_year(dt));
		}
	}

	/* set something plausible */
	if (fu_firmware_get_filename(firmware) == NULL &&
	    fu_x509_certificate_get_subject(crt) != NULL) {
		g_autofree gchar *filename = g_strdup_printf("%s_%s.der",
							     fu_firmware_get_id(FU_FIRMWARE(crt)),
							     fu_x509_certificate_get_subject(crt));
		fu_firmware_set_filename(firmware, filename);
	}

	/* success */
	return TRUE;
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

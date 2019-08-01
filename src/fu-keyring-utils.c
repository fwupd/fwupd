/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuKeyring"

#include <config.h>

#include "fwupd-error.h"

#include "fu-common.h"
#include "fu-keyring-utils.h"

#ifdef ENABLE_GPG
#include "fu-keyring-gpg.h"
#endif
#ifdef ENABLE_PKCS7
#include "fu-keyring-pkcs7.h"
#endif

/**
 * fu_keyring_create_for_kind:
 * @kind: A #FwupdKeyringKind, e.g. %FWUPD_KEYRING_KIND_GPG
 * @error: A #GError, or %NULL
 *
 * Creates a new keyring of the specified kind.
 *
 * If the keyring cannot be created (for example, if fwupd is compiled without
 * GPG support) then an error is returned.
 *
 * Returns: (transfer full): a new #FuKeyring, or %NULL for error
 **/
FuKeyring *
fu_keyring_create_for_kind (FwupdKeyringKind kind, GError **error)
{
	if (kind == FWUPD_KEYRING_KIND_GPG) {
#ifdef ENABLE_GPG
		return fu_keyring_gpg_new ();
#else
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "Not compiled with GPG support");
		return NULL;
#endif
	}
	if (kind == FWUPD_KEYRING_KIND_PKCS7) {
#ifdef ENABLE_PKCS7
		return fu_keyring_pkcs7_new ();
#else
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "Not compiled with PKCS7 support");
		return NULL;
#endif
	}
	g_set_error (error,
		     FWUPD_ERROR,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     "Keyring kind %s not supported",
		     fwupd_keyring_kind_to_string (kind));
	return NULL;
}

/**
 * fu_keyring_get_release_trust_flags:
 * @release: A #AsRelease, e.g. %FWUPD_KEYRING_KIND_GPG
 * @trust_flags: A #FwupdTrustFlags, e.g. %FWUPD_TRUST_FLAG_PAYLOAD
 * @error: A #GError, or %NULL
 *
 * Uses the correct keyring to get the trust flags for a given release.
 *
 * Returns: %TRUE if @trust_flags has been set
 **/
gboolean
fu_keyring_get_release_trust_flags (AsRelease *release,
				    FwupdTrustFlags *trust_flags,
				    GError **error)
{
	AsChecksum *csum_tmp;
	FwupdKeyringKind keyring_kind = FWUPD_KEYRING_KIND_UNKNOWN;
	GBytes *blob_payload;
	GBytes *blob_signature;
	const gchar *fn;
	g_autofree gchar *pki_dir = NULL;
	g_autofree gchar *sysconfdir = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuKeyring) kr = NULL;
	g_autoptr(FuKeyringResult) kr_result = NULL;
	struct {
		FwupdKeyringKind kind;
		const gchar *ext;
	} keyrings[] = {
		{ FWUPD_KEYRING_KIND_GPG,	"asc" },
		{ FWUPD_KEYRING_KIND_PKCS7,	"p7b" },
		{ FWUPD_KEYRING_KIND_PKCS7,	"p7c" },
		{ FWUPD_KEYRING_KIND_NONE,	NULL }
	};

	/* no filename? */
	csum_tmp = as_release_get_checksum_by_target (release, AS_CHECKSUM_TARGET_CONTENT);
	if (csum_tmp == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no content checksum for release");
		return FALSE;
	}
	fn = as_checksum_get_filename (csum_tmp);
	if (fn == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no filename");
		return FALSE;
	}

	/* no signature == no trust */
	for (guint i = 0; keyrings[i].ext != NULL; i++) {
		g_autofree gchar *fn_tmp = g_strdup_printf ("%s.%s", fn, keyrings[i].ext);
		blob_signature = as_release_get_blob (release, fn_tmp);
		if (blob_signature != NULL) {
			keyring_kind = keyrings[i].kind;
			break;
		}
	}
	if (keyring_kind == FWUPD_KEYRING_KIND_UNKNOWN) {
		g_debug ("firmware archive contained no signature");
		return TRUE;
	}

	/* get payload */
	blob_payload = as_release_get_blob (release, fn);
	if (blob_payload == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "no payload");
		return FALSE;
	}

	/* check we were installed correctly */
	sysconfdir = fu_common_get_path (FU_PATH_KIND_SYSCONFDIR);
	pki_dir = g_build_filename (sysconfdir, "pki", PACKAGE_NAME, NULL);
	if (!g_file_test (pki_dir, G_FILE_TEST_EXISTS)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "PKI directory %s not found", pki_dir);
		return FALSE;
	}

	/* verify against the system trusted keys */
	kr = fu_keyring_create_for_kind (keyring_kind, error);
	if (kr == NULL)
		return FALSE;
	if (!fu_keyring_setup (kr, error)) {
		g_prefix_error (error, "failed to set up %s keyring: ",
				fu_keyring_get_name (kr));
		return FALSE;
	}
	if (!fu_keyring_add_public_keys (kr, pki_dir, error)) {
		g_prefix_error (error, "failed to add public keys to %s keyring: ",
				fu_keyring_get_name (kr));
		return FALSE;
	}
	kr_result = fu_keyring_verify_data (kr, blob_payload, blob_signature, FU_KEYRING_VERIFY_FLAG_NONE, &error_local);
	if (kr_result == NULL) {
		g_warning ("untrusted as failed to verify from %s keyring: %s",
			   fu_keyring_get_name (kr),
			   error_local->message);
		return TRUE;
	}

	/* awesome! */
	g_debug ("marking payload as trusted");
	*trust_flags |= FWUPD_TRUST_FLAG_PAYLOAD;
	return TRUE;
}

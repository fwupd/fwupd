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
 * fu_keyring_get_release_flags:
 * @release: A #XbNode, e.g. %FWUPD_KEYRING_KIND_GPG
 * @flags: A #FwupdReleaseFlags, e.g. %FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD
 * @error: A #GError, or %NULL
 *
 * Uses the correct keyring to get the trust flags for a given release.
 *
 * Returns: %TRUE if @flags has been set
 **/
gboolean
fu_keyring_get_release_flags (XbNode *release,
			      FwupdReleaseFlags *flags,
			      GError **error)
{
	FwupdKeyringKind keyring_kind = FWUPD_KEYRING_KIND_UNKNOWN;
	GBytes *blob_payload;
	GBytes *blob_signature;
	const gchar *fn;
	g_autofree gchar *pki_dir = NULL;
	g_autofree gchar *release_key = NULL;
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

	/* custom filename specified */
	fn = xb_node_query_attr (release, "checksum[@target='content']", "filename", NULL);
	if (fn == NULL)
		fn = "filename.bin";

	/* no signature == no trust */
	for (guint i = 0; keyrings[i].ext != NULL; i++) {
		g_autofree gchar *fn_tmp = NULL;
		fn_tmp = g_strdup_printf ("fwupd::ReleaseBlob(%s.%s)",
					  fn, keyrings[i].ext);
		blob_signature = g_object_get_data (G_OBJECT (release), fn_tmp);
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
	release_key = g_strdup_printf ("fwupd::ReleaseBlob(%s)", fn);
	blob_payload = g_object_get_data (G_OBJECT (release), release_key);
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
#if defined(ENABLE_PKCS7) || defined(ENABLE_PKCS7)
	if (!g_file_test (pki_dir, G_FILE_TEST_EXISTS)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "PKI directory %s not found", pki_dir);
		return FALSE;
	}
#endif

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
	kr_result = fu_keyring_verify_data (kr, blob_payload, blob_signature,
					    FU_KEYRING_VERIFY_FLAG_NONE,
					    &error_local);
	if (kr_result == NULL) {
		g_warning ("untrusted as failed to verify from %s keyring: %s",
			   fu_keyring_get_name (kr),
			   error_local->message);
		return TRUE;
	}

	/* awesome! */
	g_debug ("marking payload as trusted");
	*flags |= FWUPD_RELEASE_FLAG_TRUSTED_PAYLOAD;
	return TRUE;
}

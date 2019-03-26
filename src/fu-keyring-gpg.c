/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuKeyring"

#include "config.h"

#include <gpgme.h>

#include "fwupd-error.h"

#include "fu-common.h"
#include "fu-keyring-gpg.h"

struct _FuKeyringGpg
{
	FuKeyring		 parent_instance;
	gpgme_ctx_t		 ctx;
};

G_DEFINE_TYPE (FuKeyringGpg, fu_keyring_gpg, FU_TYPE_KEYRING)

G_DEFINE_AUTO_CLEANUP_FREE_FUNC(gpgme_data_t, gpgme_data_release, NULL)

static gboolean
fu_keyring_gpg_add_public_key (FuKeyringGpg *self,
			       const gchar *filename,
			       GError **error)
{
	gpgme_error_t rc;
	gpgme_import_result_t result;
	gpgme_import_status_t s;
	g_auto(gpgme_data_t) data = NULL;

	/* import public key */
	g_debug ("Adding GnuPG public key %s", filename);
	rc = gpgme_data_new_from_file (&data, filename, 1);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to load %s: %s",
			     filename, gpgme_strerror (rc));
		return FALSE;
	}
	rc = gpgme_op_import (self->ctx, data);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to import %s: %s",
			     filename, gpgme_strerror (rc));
		return FALSE;
	}

	/* print what keys were imported */
	result = gpgme_op_import_result (self->ctx);
	for (s = result->imports; s != NULL; s = s->next) {
		g_debug ("importing key %s [%u] %s",
			 s->fpr, s->status, gpgme_strerror (s->result));
	}

	/* make sure keys were really imported */
	if (result->imported == 0 && result->unchanged == 0) {
		g_debug("imported: %d, unchanged: %d, not_imported: %d",
			result->imported,
			result->unchanged,
			result->not_imported);
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "key import failed %s",
			     filename);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_keyring_gpg_setup (FuKeyring *keyring, GError **error)
{
	FuKeyringGpg *self = FU_KEYRING_GPG (keyring);
	gpgme_error_t rc;
	g_autofree gchar *gpg_home = NULL;
	g_autofree gchar *localstatedir = NULL;

	if (self->ctx != NULL)
		return TRUE;

	/* startup gpgme */
	rc = gpg_err_init ();
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to init: %s",
			     gpgme_strerror (rc));
		return FALSE;
	}

	/* create a new GPG context */
	rc = gpgme_new (&self->ctx);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to create context: %s",
			     gpgme_strerror (rc));
		return FALSE;
	}

	/* set the protocol */
	rc = gpgme_set_protocol (self->ctx, GPGME_PROTOCOL_OpenPGP);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to set protocol: %s",
			     gpgme_strerror (rc));
		return FALSE;
	}

	/* set a custom home directory */
	localstatedir = fu_common_get_path (FU_PATH_KIND_LOCALSTATEDIR_PKG);
	gpg_home = g_build_filename (localstatedir, "gnupg", NULL);
	if (g_mkdir_with_parents (gpg_home, 0700) < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to create %s",
			     gpg_home);
		return FALSE;
	}
	g_debug ("Using keyring at %s", gpg_home);
	rc = gpgme_ctx_set_engine_info (self->ctx,
					GPGME_PROTOCOL_OpenPGP,
					NULL,
					gpg_home);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to set protocol: %s",
			     gpgme_strerror (rc));
		return FALSE;
	}

	/* enable armor mode */
	gpgme_set_armor (self->ctx, TRUE);
	return TRUE;
}

static gboolean
fu_keyring_gpg_add_public_keys (FuKeyring *keyring,
				const gchar *path,
				GError **error)
{
	FuKeyringGpg *self = FU_KEYRING_GPG (keyring);
	const gchar *fn_tmp;
	g_autoptr(GDir) dir = NULL;

	/* search all the public key files */
	dir = g_dir_open (path, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((fn_tmp = g_dir_read_name (dir)) != NULL) {
		g_autofree gchar *path_tmp = NULL;
		if (!g_str_has_prefix (fn_tmp, "GPG-KEY-"))
			continue;
		path_tmp = g_build_filename (path, fn_tmp, NULL);
		if (!fu_keyring_gpg_add_public_key (self, path_tmp, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_keyring_gpg_check_signature (gpgme_signature_t signature, GError **error)
{
	gboolean ret = FALSE;

	/* look at the signature status */
	switch (gpgme_err_code (signature->status)) {
	case GPG_ERR_NO_ERROR:
		ret = TRUE;
		break;
	case GPG_ERR_SIG_EXPIRED:
	case GPG_ERR_KEY_EXPIRED:
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_SIGNATURE_INVALID,
			     "valid signature '%s' has expired",
			     signature->fpr);
		break;
	case GPG_ERR_CERT_REVOKED:
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_SIGNATURE_INVALID,
			     "valid signature '%s' has been revoked",
			     signature->fpr);
		break;
	case GPG_ERR_BAD_SIGNATURE:
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_SIGNATURE_INVALID,
			     "'%s' is not a valid signature",
			     signature->fpr);
		break;
	case GPG_ERR_NO_PUBKEY:
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_SIGNATURE_INVALID,
			     "Could not check signature '%s' as no public key",
			     signature->fpr);
		break;
	default:
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_SIGNATURE_INVALID,
			     "gpgme failed to verify signature '%s'",
			     signature->fpr);
		break;
	}
	return ret;
}

static FuKeyringResult *
fu_keyring_gpg_verify_data (FuKeyring *keyring,
			    GBytes *blob,
			    GBytes *blob_signature,
			    FuKeyringVerifyFlags flags,
			    GError **error)
{
	FuKeyringGpg *self = FU_KEYRING_GPG (keyring);
	gpgme_error_t rc;
	gpgme_signature_t s;
	gpgme_verify_result_t result;
	gint64 timestamp_newest = 0;
	g_auto(gpgme_data_t) data = NULL;
	g_auto(gpgme_data_t) sig = NULL;
	g_autoptr(GString) authority_newest = g_string_new (NULL);

	/* not supported */
	if (flags & FU_KEYRING_VERIFY_FLAG_USE_CLIENT_CERT) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "no GPG client certificate support");
		return NULL;
	}

	/* load file data */
	rc = gpgme_data_new_from_mem (&data,
				      g_bytes_get_data (blob, NULL),
				      g_bytes_get_size (blob), 0);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to load data: %s",
			     gpgme_strerror (rc));
		return NULL;
	}
	rc = gpgme_data_new_from_mem (&sig,
				      g_bytes_get_data (blob_signature, NULL),
				      g_bytes_get_size (blob_signature), 0);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to load signature: %s",
			      gpgme_strerror (rc));
		return NULL;
	}

	/* verify */
	rc = gpgme_op_verify (self->ctx, sig, data, NULL);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to verify data: %s",
			     gpgme_strerror (rc));
		return NULL;
	}


	/* verify the result */
	result = gpgme_op_verify_result (self->ctx);
	if (result == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no result record from libgpgme");
		return NULL;
	}

	/* look at each signature */
	for (s = result->signatures; s != NULL ; s = s->next ) {
		g_debug ("returned signature fingerprint %s", s->fpr);
		if (!fu_keyring_gpg_check_signature (s, error))
			return NULL;

		/* save details about the key for the result */
		if ((gint64) s->timestamp > timestamp_newest) {
			timestamp_newest = (gint64) s->timestamp;
			g_string_assign (authority_newest, s->fpr);
		}
	}
	return FU_KEYRING_RESULT (g_object_new (FU_TYPE_KEYRING_RESULT,
						"timestamp", timestamp_newest,
						"authority", authority_newest->str,
						NULL));
}

static void
fu_keyring_gpg_finalize (GObject *object)
{
	FuKeyringGpg *self = FU_KEYRING_GPG (object);
	if (self->ctx != NULL)
		gpgme_release (self->ctx);
	G_OBJECT_CLASS (fu_keyring_gpg_parent_class)->finalize (object);
}

static void
fu_keyring_gpg_class_init (FuKeyringGpgClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuKeyringClass *klass_app = FU_KEYRING_CLASS (klass);
	klass_app->setup = fu_keyring_gpg_setup;
	klass_app->add_public_keys = fu_keyring_gpg_add_public_keys;
	klass_app->verify_data = fu_keyring_gpg_verify_data;
	object_class->finalize = fu_keyring_gpg_finalize;
}

static void
fu_keyring_gpg_init (FuKeyringGpg *self)
{
	FuKeyring *keyring = FU_KEYRING (self);
	g_autofree gchar *name = NULL;
	name = g_strdup_printf ("gpgme-v%s", gpgme_check_version (NULL));
	fu_keyring_set_name (keyring, name);
}

FuKeyring *
fu_keyring_gpg_new (void)
{
	return FU_KEYRING (g_object_new (FU_TYPE_KEYRING_GPG, NULL));
}

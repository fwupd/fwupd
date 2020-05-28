/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <fwupd.h>
#include <gpgme.h>

#include "fu-keyring.h"

static void fu_keyring_finalize			 (GObject *object);

/**
 * FuKeyringPrivate:
 *
 * Private #FuKeyring data
 **/
typedef struct {
	gpgme_ctx_t		 ctx;
} FuKeyringPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuKeyring, fu_keyring, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_keyring_get_instance_private (o))

/**
 * fu_keyring_setup:
 **/
static gboolean
fu_keyring_setup (FuKeyring *keyring, GError **error)
{
	FuKeyringPrivate *priv = GET_PRIVATE (keyring);
	gpgme_error_t rc;

	g_return_val_if_fail (FU_IS_KEYRING (keyring), FALSE);

	if (priv->ctx != NULL)
		return TRUE;

	/* check version */
	gpgme_check_version (NULL);

	/* startup gpgme */
	rc = gpg_err_init ();
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to startup GPG: %s",
			     gpgme_strerror (rc));
		return FALSE;
	}

	/* create a new GPG context */
	rc = gpgme_new (&priv->ctx);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to create context: %s",
			     gpgme_strerror (rc));
		return FALSE;
	}

	/* set the protocol */
	rc = gpgme_set_protocol (priv->ctx, GPGME_PROTOCOL_OpenPGP);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to set protocol: %s",
			     gpgme_strerror (rc));
		return FALSE;
	}

	/* enable armor mode */
	gpgme_set_armor (priv->ctx, TRUE);

	/* never interactive */
	gpgme_set_pinentry_mode (priv->ctx, GPGME_PINENTRY_MODE_ERROR);

	return TRUE;
}

/**
 * fu_keyring_add_public_key:
 **/
gboolean
fu_keyring_add_public_key (FuKeyring *keyring, const gchar *filename, GError **error)
{
	FuKeyringPrivate *priv = GET_PRIVATE (keyring);
	gboolean ret = TRUE;
	gpgme_data_t data = NULL;
	gpgme_error_t rc;
	gpgme_import_result_t result;
	gpgme_import_status_t s;

	g_return_val_if_fail (FU_IS_KEYRING (keyring), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);

	/* import public key */
	g_debug ("Adding public key %s", filename);
	rc = gpgme_data_new_from_file (&data, filename, 1);
	if (rc != GPG_ERR_NO_ERROR) {
		ret = FALSE;
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to load %s: %s",
			     filename, gpgme_strerror (rc));
		goto out;
	}
	rc = gpgme_op_import (priv->ctx, data);
	if (rc != GPG_ERR_NO_ERROR) {
		ret = FALSE;
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to import %s: %s",
			     filename, gpgme_strerror (rc));
		goto out;
	}

	/* print what keys were imported */
	result = gpgme_op_import_result (priv->ctx);
	for (s = result->imports; s != NULL; s = s->next) {
		g_debug ("importing key %s [%i] %s",
			 s->fpr, s->status, gpgme_strerror (s->result));
	}
out:
	gpgme_data_release (data);
	return ret;
}

/**
 * fu_keyring_add_public_keys:
 **/
gboolean
fu_keyring_add_public_keys (FuKeyring *keyring, const gchar *dirname, GError **error)
{
	g_autoptr(GDir) dir = NULL;

	g_return_val_if_fail (FU_IS_KEYRING (keyring), FALSE);
	g_return_val_if_fail (dirname != NULL, FALSE);

	/* setup context */
	if (!fu_keyring_setup (keyring, error))
		return FALSE;

	/* search all the public key files */
	dir = g_dir_open (dirname, 0, error);
	if (dir == NULL)
		return FALSE;
	do {
		const gchar *filename;
		g_autofree gchar *path_tmp = NULL;
		filename = g_dir_read_name (dir);
		if (filename == NULL)
			break;
		path_tmp = g_build_filename (dirname, filename, NULL);
		if (!fu_keyring_add_public_key (keyring, path_tmp, error))
			return FALSE;
	} while (TRUE);
	return TRUE;
}

/**
 * fu_keyring_check_signature:
 **/
static gboolean
fu_keyring_check_signature (gpgme_signature_t signature, GError **error)
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

/**
 * fu_keyring_verify_file:
 **/
gboolean
fu_keyring_verify_file (FuKeyring *keyring,
			const gchar *filename,
			const gchar *signature,
			GError **error)
{
	FuKeyringPrivate *priv = GET_PRIVATE (keyring);
	gboolean has_header;
	gboolean ret = TRUE;
	gpgme_data_t data = NULL;
	gpgme_data_t sig = NULL;
	gpgme_error_t rc;
	gpgme_signature_t s;
	gpgme_verify_result_t result;
	g_autoptr(GString) sig_v1 = NULL;

	g_return_val_if_fail (FU_IS_KEYRING (keyring), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (signature != NULL, FALSE);

	/* setup context */
	if (!fu_keyring_setup (keyring, error))
		return FALSE;

	/* has header already */
	has_header = g_strstr_len (signature, -1, "BEGIN PGP SIGNATURE") != NULL;

	/* load file data */
	rc = gpgme_data_new_from_file (&data, filename, 1);
	if (rc != GPG_ERR_NO_ERROR) {
		ret = FALSE;
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to load %s: %s",
			     filename, gpgme_strerror (rc));
		goto out;
	}

	/* load signature */
	sig_v1 = g_string_new ("");
	if (!has_header) {
		g_string_append (sig_v1, "-----BEGIN PGP SIGNATURE-----\n");
		g_string_append (sig_v1, "Version: GnuPG v1\n\n");
	}
	g_string_append_printf (sig_v1, "%s\n", signature);
	if (!has_header)
		g_string_append (sig_v1, "-----END PGP SIGNATURE-----\n");
	rc = gpgme_data_new_from_mem (&sig, sig_v1->str, sig_v1->len, 0);
	if (rc != GPG_ERR_NO_ERROR) {
		ret = FALSE;
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to load signature %s: %s",
			     signature, gpgme_strerror (rc));
		goto out;
	}

	/* verify */
	rc = gpgme_op_verify (priv->ctx, sig, data, NULL);
	if (rc != GPG_ERR_NO_ERROR) {
		ret = FALSE;
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to verify %s: %s",
			     filename, gpgme_strerror (rc));
		goto out;
	}


	/* verify the result */
	result = gpgme_op_verify_result (priv->ctx);
	if (result == NULL) {
		ret = FALSE;
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no result record from libgpgme");
		goto out;
	}
	if (result->signatures == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no signatures from libgpgme");
		return FALSE;
	}

	/* look at each signature */
	for (s = result->signatures; s != NULL ; s = s->next ) {
		ret = fu_keyring_check_signature (s, error);
		if (!ret)
			goto out;
	}
out:
	if (data != NULL)
		gpgme_data_release (data);
	if (sig != NULL)
		gpgme_data_release (sig);
	return ret;
}

/**
 * fu_keyring_verify_data:
 **/
gboolean
fu_keyring_verify_data (FuKeyring *keyring,
			GBytes *payload,
			GBytes *payload_signature,
			GError **error)
{
	FuKeyringPrivate *priv = GET_PRIVATE (keyring);
	gboolean ret = TRUE;
	gpgme_data_t data = NULL;
	gpgme_data_t sig = NULL;
	gpgme_error_t rc;
	gpgme_signature_t s;
	gpgme_verify_result_t result;

	g_return_val_if_fail (FU_IS_KEYRING (keyring), FALSE);
	g_return_val_if_fail (payload != NULL, FALSE);
	g_return_val_if_fail (payload_signature != NULL, FALSE);

	/* setup context */
	if (!fu_keyring_setup (keyring, error))
		return FALSE;

	/* load file data */
	rc = gpgme_data_new_from_mem (&data,
				      g_bytes_get_data (payload, NULL),
				      g_bytes_get_size (payload), 0);
	if (rc != GPG_ERR_NO_ERROR) {
		ret = FALSE;
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to load data: %s",
			     gpgme_strerror (rc));
		goto out;
	}
	rc = gpgme_data_new_from_mem (&sig,
				      g_bytes_get_data (payload_signature, NULL),
				      g_bytes_get_size (payload_signature), 0);
	if (rc != GPG_ERR_NO_ERROR) {
		ret = FALSE;
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to load signature: %s",
			      gpgme_strerror (rc));
		goto out;
	}

	/* verify */
	rc = gpgme_op_verify (priv->ctx, sig, data, NULL);
	if (rc != GPG_ERR_NO_ERROR) {
		ret = FALSE;
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to verify data: %s",
			     gpgme_strerror (rc));
		goto out;
	}


	/* verify the result */
	result = gpgme_op_verify_result (priv->ctx);
	if (result == NULL) {
		ret = FALSE;
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no result record from libgpgme");
		goto out;
	}

	/* look at each signature */
	for (s = result->signatures; s != NULL ; s = s->next ) {
		ret = fu_keyring_check_signature (s, error);
		if (!ret)
			goto out;
	}
out:
	if (data != NULL)
		gpgme_data_release (data);
	if (sig != NULL)
		gpgme_data_release (sig);
	return ret;
}

/**
 * fu_keyring_class_init:
 **/
static void
fu_keyring_class_init (FuKeyringClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_keyring_finalize;
}

/**
 * fu_keyring_init:
 **/
static void
fu_keyring_init (FuKeyring *keyring)
{
}

/**
 * fu_keyring_finalize:
 **/
static void
fu_keyring_finalize (GObject *object)
{
	FuKeyring *keyring = FU_KEYRING (object);
	FuKeyringPrivate *priv = GET_PRIVATE (keyring);

	if (priv->ctx != NULL)
		gpgme_release (priv->ctx);

	G_OBJECT_CLASS (fu_keyring_parent_class)->finalize (object);
}

/**
 * fu_keyring_new:
 **/
FuKeyring *
fu_keyring_new (void)
{
	FuKeyring *keyring;
	keyring = g_object_new (FU_TYPE_KEYRING, NULL);
	return FU_KEYRING (keyring);
}

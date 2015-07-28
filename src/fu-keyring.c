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

#include "fu-cleanup.h"
#include "fu-keyring.h"

static void fu_keyring_finalize			 (GObject *object);

#define FU_KEYRING_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), FU_TYPE_KEYRING, FuKeyringPrivate))

/**
 * FuKeyringPrivate:
 *
 * Private #FuKeyring data
 **/
struct _FuKeyringPrivate
{
	gpgme_ctx_t		 ctx;
};

G_DEFINE_TYPE (FuKeyring, fu_keyring, G_TYPE_OBJECT)

/**
 * fu_keyring_setup:
 **/
static gboolean
fu_keyring_setup (FuKeyring *keyring, GError **error)
{
	gpgme_error_t rc;

	g_return_val_if_fail (FU_IS_KEYRING (keyring), FALSE);

	if (keyring->priv->ctx != NULL)
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
	rc = gpgme_new (&keyring->priv->ctx);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to create context: %s",
			     gpgme_strerror (rc));
		return FALSE;
	}

	/* set the protocol */
	rc = gpgme_set_protocol (keyring->priv->ctx, GPGME_PROTOCOL_OpenPGP);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to set protocol: %s",
			     gpgme_strerror (rc));
		return FALSE;
	}

	/* enable armor mode */
	gpgme_set_armor (keyring->priv->ctx, TRUE);

	/* never interactive */
	gpgme_set_pinentry_mode (keyring->priv->ctx, GPGME_PINENTRY_MODE_ERROR);

	return TRUE;
}

/**
 * fu_keyring_list_private_keys:
 **/
static void
fu_keyring_list_private_keys (FuKeyring *keyring)
{
	gpgme_key_t key;
	gpgme_error_t err;

	err = gpgme_op_keylist_start (keyring->priv->ctx, NULL, 1);
	while (!err) {
		_cleanup_string_free_ GString *str = NULL;
		err = gpgme_op_keylist_next (keyring->priv->ctx, &key);
		if (err)
			break;
		str = g_string_new (key->subkeys->keyid);
		g_string_append_printf (str, "\t[secret:%i, sign:%i]",
					key->subkeys->secret,
					key->subkeys->can_sign);
		if (key->uids && key->uids->name)
			g_string_append_printf (str,  " %s", key->uids->name);
		if (key->uids && key->uids->email)
			g_string_append_printf (str,  " <%s>", key->uids->email);
		g_debug ("%s", str->str);
		gpgme_key_release (key);
	}

	if (gpg_err_code (err) != GPG_ERR_EOF) {
		g_warning ("can not list keys: %s\n", gpgme_strerror (err));
		return;
	}
}

/**
 * fu_keyring_set_signing_key:
 **/
gboolean
fu_keyring_set_signing_key (FuKeyring *keyring, const gchar *key_id, GError **error)
{
	gint n_signers;
	gpgme_error_t rc;
	gpgme_key_t key;


	/* setup context */
	if (!fu_keyring_setup (keyring, error))
		return FALSE;

	/* list possible keys */
	fu_keyring_list_private_keys (keyring);

	/* find key */
	rc = gpgme_get_key (keyring->priv->ctx, key_id, &key, 1);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to find key %s: %s",
			     key_id, gpgme_strerror (rc));
		return FALSE;
	}

	/* select it to be used */
	gpgme_signers_clear (keyring->priv->ctx);
	rc = gpgme_signers_add (keyring->priv->ctx, key);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to add signing key %s: %s",
			     key_id, gpgme_strerror (rc));
		return FALSE;
	}
	gpgme_key_unref (key);

	/* check it's selected */
	n_signers = gpgme_signers_count (keyring->priv->ctx);
	if (n_signers != 1) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to check signing key %s", key_id);
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_keyring_add_public_key:
 **/
gboolean
fu_keyring_add_public_key (FuKeyring *keyring, const gchar *filename, GError **error)
{
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
	rc = gpgme_op_import (keyring->priv->ctx, data);
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
	result = gpgme_op_import_result (keyring->priv->ctx);
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
	_cleanup_dir_close_ GDir *dir = NULL;

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
		_cleanup_free_ gchar *path_tmp = NULL;
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
	gboolean has_header;
	gboolean ret = TRUE;
	gpgme_data_t data = NULL;
	gpgme_data_t sig = NULL;
	gpgme_error_t rc;
	gpgme_signature_t s;
	gpgme_verify_result_t result;
	_cleanup_string_free_ GString *sig_v1 = NULL;

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
	rc = gpgme_op_verify (keyring->priv->ctx, sig, data, NULL);
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
	result = gpgme_op_verify_result (keyring->priv->ctx);
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
 * fu_keyring_sign_data:
 **/
GBytes *
fu_keyring_sign_data (FuKeyring *keyring, GBytes *payload, GError **error)
{
	GBytes *sig_bytes = NULL;
	gchar *sig_data = NULL;
	gpgme_data_t data = NULL;
	gpgme_data_t sig = NULL;
	gpgme_error_t rc;
	gpgme_sign_result_t sign_result;
	gsize sig_len = 0;

	g_return_val_if_fail (FU_IS_KEYRING (keyring), FALSE);
	g_return_val_if_fail (payload != NULL, FALSE);

	/* setup context */
	if (!fu_keyring_setup (keyring, error))
		return NULL;

	/* load file data */
	rc = gpgme_data_new_from_mem (&data,
				      g_bytes_get_data (payload, NULL),
				      g_bytes_get_size (payload), 0);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to load data: %s",
			     gpgme_strerror (rc));
		goto out;
	}

	/* sign */
	gpgme_data_new (&sig);
	rc = gpgme_op_sign (keyring->priv->ctx, data, sig, GPGME_SIG_MODE_DETACH);
	if (rc != GPG_ERR_NO_ERROR) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to sign data: %s",
			     gpgme_strerror (rc));
		goto out;
	}
	sign_result = gpgme_op_sign_result (keyring->priv->ctx);
	if (sign_result == NULL ||
	    sign_result->signatures == NULL ||
	    sign_result->signatures->fpr == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "failed to sign data with any key");
		goto out;
	}
	g_debug ("signed with key %s", sign_result->signatures->fpr);

	/* steal signature data */
	sig_data = gpgme_data_release_and_get_mem (sig, &sig_len);
	sig_bytes = g_bytes_new_with_free_func (sig_data, sig_len, (GDestroyNotify) gpgme_free, sig_data);
	sig = NULL;
out:
	if (data != NULL)
		gpgme_data_release (data);
	if (sig != NULL)
		gpgme_data_release (sig);
	return sig_bytes;
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
	rc = gpgme_op_verify (keyring->priv->ctx, sig, data, NULL);
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
	result = gpgme_op_verify_result (keyring->priv->ctx);
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
	g_type_class_add_private (klass, sizeof (FuKeyringPrivate));
}

/**
 * fu_keyring_init:
 **/
static void
fu_keyring_init (FuKeyring *keyring)
{
	keyring->priv = FU_KEYRING_GET_PRIVATE (keyring);
}

/**
 * fu_keyring_finalize:
 **/
static void
fu_keyring_finalize (GObject *object)
{
	FuKeyring *keyring = FU_KEYRING (object);
	FuKeyringPrivate *priv = keyring->priv;

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

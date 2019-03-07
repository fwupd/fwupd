/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuKeyring"

#include "config.h"

#include "fwupd-error.h"

#include "fu-keyring.h"

typedef struct {
	gchar		*name;
} FuKeyringPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (FuKeyring, fu_keyring, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_keyring_get_instance_private (o))

gboolean
fu_keyring_setup (FuKeyring *keyring, GError **error)
{
	FuKeyringClass *klass = FU_KEYRING_GET_CLASS (keyring);
	g_return_val_if_fail (FU_IS_KEYRING (keyring), FALSE);
	return klass->setup (keyring, error);
}

gboolean
fu_keyring_add_public_keys (FuKeyring *keyring, const gchar *path, GError **error)
{
	FuKeyringClass *klass = FU_KEYRING_GET_CLASS (keyring);
	g_return_val_if_fail (FU_IS_KEYRING (keyring), FALSE);
	g_return_val_if_fail (path != NULL, FALSE);
	return klass->add_public_keys (keyring, path, error);
}

FuKeyringResult *
fu_keyring_verify_data (FuKeyring *keyring,
		       GBytes *blob,
		       GBytes *blob_signature,
		       FuKeyringVerifyFlags flags,
		       GError **error)
{
	FuKeyringClass *klass = FU_KEYRING_GET_CLASS (keyring);
	g_return_val_if_fail (FU_IS_KEYRING (keyring), NULL);
	g_return_val_if_fail (blob != NULL, NULL);
	g_return_val_if_fail (blob_signature != NULL, NULL);
	return klass->verify_data (keyring, blob, blob_signature, flags, error);
}

GBytes *
fu_keyring_sign_data (FuKeyring *keyring,
		      GBytes *blob,
		      FuKeyringSignFlags flags,
		      GError **error)
{
	FuKeyringClass *klass = FU_KEYRING_GET_CLASS (keyring);
	g_return_val_if_fail (FU_IS_KEYRING (keyring), NULL);
	g_return_val_if_fail (blob != NULL, NULL);
	if (klass->sign_data == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "signing data is not supported");
		return NULL;
	}
	return klass->sign_data (keyring, blob, flags, error);
}

const gchar *
fu_keyring_get_name (FuKeyring *self)
{
	FuKeyringPrivate *priv = GET_PRIVATE (self);
	return priv->name;
}

void
fu_keyring_set_name (FuKeyring *self, const gchar *name)
{
	FuKeyringPrivate *priv = GET_PRIVATE (self);
	g_free (priv->name);
	priv->name = g_strdup (name);
}

static void
fu_keyring_finalize (GObject *object)
{
	FuKeyring *self = FU_KEYRING (object);
	FuKeyringPrivate *priv = GET_PRIVATE (self);
	g_free (priv->name);
	G_OBJECT_CLASS (fu_keyring_parent_class)->finalize (object);
}

static void
fu_keyring_class_init (FuKeyringClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_keyring_finalize;
}

static void
fu_keyring_init (FuKeyring *keyring)
{
}

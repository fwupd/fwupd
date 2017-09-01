/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
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
		       GError **error)
{
	FuKeyringClass *klass = FU_KEYRING_GET_CLASS (keyring);
	g_return_val_if_fail (FU_IS_KEYRING (keyring), NULL);
	g_return_val_if_fail (blob != NULL, NULL);
	g_return_val_if_fail (blob_signature != NULL, NULL);
	return klass->verify_data (keyring, blob, blob_signature, error);
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

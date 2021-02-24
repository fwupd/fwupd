/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuBackend"

#include "config.h"

#include "fu-backend.h"

typedef struct {
	gchar				*name;
	gboolean			 enabled;
	GHashTable			*devices;	/* device_id : * FuDevice */
} FuBackendPrivate;

enum {
	SIGNAL_ADDED,
	SIGNAL_REMOVED,
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_NAME,
	PROP_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (FuBackend, fu_backend, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_backend_get_instance_private (o))

void
fu_backend_device_added (FuBackend *self, FuDevice *device)
{
	FuBackendPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_BACKEND (self));
	g_return_if_fail (FU_IS_DEVICE (device));
	g_hash_table_insert (priv->devices,
			     g_strdup (fu_device_get_backend_id (device)),
			     g_object_ref (device));
	g_signal_emit (self, signals[SIGNAL_ADDED], 0, device);
}

void
fu_backend_device_removed (FuBackend *self, FuDevice *device)
{
	FuBackendPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_BACKEND (self));
	g_return_if_fail (FU_IS_DEVICE (device));
	g_signal_emit (self, signals[SIGNAL_REMOVED], 0, device);
	g_hash_table_remove (priv->devices, fu_device_get_backend_id (device));
}

void
fu_backend_device_changed (FuBackend *self, FuDevice *device)
{
	g_return_if_fail (FU_IS_BACKEND (self));
	g_return_if_fail (FU_IS_DEVICE (device));
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0, device);
}

gboolean
fu_backend_setup (FuBackend *self, GError **error)
{
	FuBackendClass *klass = FU_BACKEND_GET_CLASS (self);
	FuBackendPrivate *priv = GET_PRIVATE (self);

	g_return_val_if_fail (FU_IS_BACKEND (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (klass->setup == NULL)
		return TRUE;
	if (!klass->setup (self, error)) {
		priv->enabled = FALSE;
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_backend_coldplug (FuBackend *self, GError **error)
{
	FuBackendClass *klass = FU_BACKEND_GET_CLASS (self);
	g_return_val_if_fail (FU_IS_BACKEND (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	if (klass->coldplug == NULL)
		return TRUE;
	return klass->coldplug (self, error);
}

gboolean
fu_backend_recoldplug (FuBackend *self, GError **error)
{
	FuBackendClass *klass = FU_BACKEND_GET_CLASS (self);
	g_return_val_if_fail (FU_IS_BACKEND (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	if (klass->recoldplug == NULL)
		return TRUE;
	return klass->recoldplug (self, error);
}

const gchar *
fu_backend_get_name (FuBackend *self)
{
	FuBackendPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_BACKEND (self), NULL);
	return priv->name;
}

gboolean
fu_backend_get_enabled (FuBackend *self)
{
	FuBackendPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_BACKEND (self), FALSE);
	return priv->enabled;
}

void
fu_backend_set_enabled (FuBackend *self, gboolean enabled)
{
	FuBackendPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_BACKEND (self));
	priv->enabled = FALSE;
}

/* (transfer none) */
FuDevice *
fu_backend_lookup_by_id (FuBackend *self, const gchar *device_id)
{
	FuBackendPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_BACKEND (self), NULL);
	return g_hash_table_lookup (priv->devices, device_id);
}

static void
fu_backend_get_property (GObject *object, guint prop_id,
			 GValue *value, GParamSpec *pspec)
{
	FuBackend *self = FU_BACKEND (object);
	FuBackendPrivate *priv = GET_PRIVATE (self);
	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_backend_set_property (GObject *object, guint prop_id,
			 const GValue *value, GParamSpec *pspec)
{
	FuBackend *self = FU_BACKEND (object);
	FuBackendPrivate *priv = GET_PRIVATE (self);
	switch (prop_id) {
	case PROP_NAME:
		priv->name = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_backend_init (FuBackend *self)
{
	FuBackendPrivate *priv = GET_PRIVATE (self);
	priv->enabled = TRUE;
	priv->devices = g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free,
					       (GDestroyNotify) g_object_unref);
}

static void
fu_backend_finalize (GObject *object)
{
	FuBackend *self = FU_BACKEND (object);
	FuBackendPrivate *priv = GET_PRIVATE (self);
	g_free (priv->name);
	g_hash_table_unref (priv->devices);
	G_OBJECT_CLASS (fu_backend_parent_class)->finalize (object);
}

static void
fu_backend_class_init (FuBackendClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamSpec *pspec;

	object_class->get_property = fu_backend_get_property;
	object_class->set_property = fu_backend_set_property;
	object_class->finalize = fu_backend_finalize;

	pspec = g_param_spec_string ("name", NULL, NULL, NULL,
				     G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_READWRITE |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_NAME, pspec);

	signals[SIGNAL_ADDED] =
		g_signal_new ("device-added",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);
	signals[SIGNAL_REMOVED] =
		g_signal_new ("device-removed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);
	signals[SIGNAL_CHANGED] =
		g_signal_new ("device-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);
}

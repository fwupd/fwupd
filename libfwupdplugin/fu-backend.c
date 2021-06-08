/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuBackend"

#include "config.h"

#include "fu-backend.h"

typedef struct {
	FuContext			*ctx;
	gchar				*name;
	gboolean			 enabled;
	gboolean			 done_setup;
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
	PROP_CONTEXT,
	PROP_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (FuBackend, fu_backend, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_backend_get_instance_private (o))

/**
 * fu_backend_device_added:
 * @self: a #FuBackend
 * @device: a device
 *
 * Emits a signal that indicates the device has been added.
 *
 * Since: 1.6.1
 **/
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

/**
 * fu_backend_device_removed:
 * @self: a #FuBackend
 * @device: a device
 *
 * Emits a signal that indicates the device has been removed.
 *
 * Since: 1.6.1
 **/
void
fu_backend_device_removed (FuBackend *self, FuDevice *device)
{
	FuBackendPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_BACKEND (self));
	g_return_if_fail (FU_IS_DEVICE (device));
	g_signal_emit (self, signals[SIGNAL_REMOVED], 0, device);
	g_hash_table_remove (priv->devices, fu_device_get_backend_id (device));
}

/**
 * fu_backend_device_changed:
 * @self: a #FuBackend
 * @device: a device
 *
 * Emits a signal that indicates the device has been changed.
 *
 * Since: 1.6.1
 **/
void
fu_backend_device_changed (FuBackend *self, FuDevice *device)
{
	g_return_if_fail (FU_IS_BACKEND (self));
	g_return_if_fail (FU_IS_DEVICE (device));
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0, device);
}

/**
 * fu_backend_setup:
 * @self: a #FuBackend
 * @error: (nullable): optional return location for an error
 *
 * Sets up the backend ready for use, which typically calls the subclassed setup
 * function. No devices should be added or removed at this point.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.6.1
 **/
gboolean
fu_backend_setup (FuBackend *self, GError **error)
{
	FuBackendClass *klass = FU_BACKEND_GET_CLASS (self);
	FuBackendPrivate *priv = GET_PRIVATE (self);

	g_return_val_if_fail (FU_IS_BACKEND (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (priv->done_setup)
		return TRUE;
	if (klass->setup != NULL) {
		if (!klass->setup (self, error)) {
			priv->enabled = FALSE;
			return FALSE;
		}
	}
	priv->done_setup = TRUE;
	return TRUE;
}

/**
 * fu_backend_coldplug:
 * @self: a #FuBackend
 * @error: (nullable): optional return location for an error
 *
 * Adds devices using the subclassed backend. If fu_backend_setup() has not
 * already been called then it is run before this function automatically.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.6.1
 **/
gboolean
fu_backend_coldplug (FuBackend *self, GError **error)
{
	FuBackendClass *klass = FU_BACKEND_GET_CLASS (self);
	g_return_val_if_fail (FU_IS_BACKEND (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
	if (!fu_backend_setup (self, error))
		return FALSE;
	if (klass->coldplug == NULL)
		return TRUE;
	return klass->coldplug (self, error);
}

/**
 * fu_backend_get_name:
 * @self: a #FuBackend
 *
 * Return the name of the backend, which is normally set by the subclass.
 *
 * Returns: backend name
 *
 * Since: 1.6.1
 **/
const gchar *
fu_backend_get_name (FuBackend *self)
{
	FuBackendPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_BACKEND (self), NULL);
	return priv->name;
}

/**
 * fu_backend_get_context:
 * @self: a #FuBackend
 *
 * Gets the context for a backend.
 *
 * Returns: (transfer none): a #FuContext or %NULL if not set
 *
 * Since: 1.6.1
 **/
FuContext *
fu_backend_get_context (FuBackend *self)
{
	FuBackendPrivate *priv = GET_PRIVATE (self);
	return priv->ctx;
}

/**
 * fu_backend_get_enabled:
 * @self: a #FuBackend
 *
 * Return the boolean value of a key if it's been configured
 *
 * Returns: %TRUE if the backend is enabled
 *
 * Since: 1.6.1
 **/
gboolean
fu_backend_get_enabled (FuBackend *self)
{
	FuBackendPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_BACKEND (self), FALSE);
	return priv->enabled;
}

/**
 * fu_backend_set_enabled:
 * @self: a #FuBackend
 * @enabled: enabled state
 *
 * Sets the backend enabled state.
 *
 * Since: 1.6.1
 **/
void
fu_backend_set_enabled (FuBackend *self, gboolean enabled)
{
	FuBackendPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_BACKEND (self));
	priv->enabled = FALSE;
}

/**
 * fu_backend_lookup_by_id:
 * @self: a #FuBackend
 * @device_id: a DeviceID
 *
 * Gets a device previously added by the backend.
 *
 * Returns: (transfer none): device, or %NULL if not found
 *
 * Since: 1.6.1
 **/
FuDevice *
fu_backend_lookup_by_id (FuBackend *self, const gchar *device_id)
{
	FuBackendPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_BACKEND (self), NULL);
	return g_hash_table_lookup (priv->devices, device_id);
}

static gint
fu_backend_get_devices_sort_cb (gconstpointer a, gconstpointer b)
{
	FuDevice *deva = *((FuDevice **) a);
	FuDevice *devb = *((FuDevice **) b);
	return g_strcmp0 (fu_device_get_backend_id (deva),
			  fu_device_get_backend_id (devb));
}

/**
 * fu_backend_get_devices:
 * @self: a #FuBackend
 *
 * Gets all the devices added by the backend.
 *
 * Returns: (transfer container) (element-type FuDevice): devices
 *
 * Since: 1.6.1
 **/
GPtrArray *
fu_backend_get_devices (FuBackend *self)
{
	FuBackendPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GList) values = NULL;
	g_autoptr(GPtrArray) devices = NULL;

	g_return_val_if_fail (FU_IS_BACKEND (self), NULL);

	devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	values = g_hash_table_get_values (priv->devices);
	for (GList *l = values; l != NULL; l = l->next)
		g_ptr_array_add (devices, g_object_ref (l->data));
	g_ptr_array_sort (devices, fu_backend_get_devices_sort_cb);
	return g_steal_pointer (&devices);
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
	case PROP_CONTEXT:
		g_value_set_object (value, priv->ctx);
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
	case PROP_CONTEXT:
		g_set_object (&priv->ctx, g_value_get_object (value));
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
	if (priv->ctx != NULL)
		g_object_unref (priv->ctx);
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

	pspec = g_param_spec_object ("context", NULL, NULL,
				     FU_TYPE_CONTEXT,
				     G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_READWRITE |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_CONTEXT, pspec);

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

/*
 * Copyright (C) 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuDevice"

#include "config.h"

#include <string.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "fu-common.h"
#include "fu-common-version.h"
#include "fu-device-private.h"
#include "fu-mutex.h"

#include "fwupd-common.h"
#include "fwupd-device-private.h"

#define FU_DEVICE_RETRY_OPEN_COUNT			5
#define FU_DEVICE_RETRY_OPEN_DELAY			500 /* ms */

/**
 * SECTION:fu-device
 * @short_description: a physical or logical device
 *
 * An object that represents a physical or logical device.
 *
 * See also: #FuDeviceLocker
 */

static void fu_device_finalize			 (GObject *object);

typedef struct {
	gchar				*alternate_id;
	gchar				*equivalent_id;
	gchar				*physical_id;
	gchar				*logical_id;
	gchar				*backend_id;
	gchar				*proxy_guid;
	FuDevice			*alternate;
	FuDevice			*proxy;		/* noref */
	FuQuirks			*quirks;
	GHashTable			*metadata;	/* (nullable) */
	GRWLock				 metadata_mutex;
	GPtrArray			*parent_guids;
	GRWLock				 parent_guids_mutex;
	guint				 remove_delay;	/* ms */
	guint				 progress;
	gint				 order;
	guint				 priority;
	guint				 poll_id;
	gboolean			 done_probe;
	gboolean			 done_setup;
	gboolean			 device_id_valid;
	guint64				 size_min;
	guint64				 size_max;
	gint				 open_refcount;	/* atomic */
	GType				 specialized_gtype;
	GPtrArray			*possible_plugins;
	GPtrArray			*retry_recs;	/* of FuDeviceRetryRecovery */
	guint				 retry_delay;
	FuDeviceInternalFlags		 internal_flags;
} FuDevicePrivate;

typedef struct {
	GQuark				 domain;
	gint				 code;
	FuDeviceRetryFunc		 recovery_func;
} FuDeviceRetryRecovery;

enum {
	PROP_0,
	PROP_PROGRESS,
	PROP_PHYSICAL_ID,
	PROP_LOGICAL_ID,
	PROP_BACKEND_ID,
	PROP_QUIRKS,
	PROP_PROXY,
	PROP_LAST
};

G_DEFINE_TYPE_WITH_PRIVATE (FuDevice, fu_device, FWUPD_TYPE_DEVICE)
#define GET_PRIVATE(o) (fu_device_get_instance_private (o))

static void
fu_device_get_property (GObject *object, guint prop_id,
			GValue *value, GParamSpec *pspec)
{
	FuDevice *self = FU_DEVICE (object);
	FuDevicePrivate *priv = GET_PRIVATE (self);
	switch (prop_id) {
	case PROP_PROGRESS:
		g_value_set_uint (value, priv->progress);
		break;
	case PROP_PHYSICAL_ID:
		g_value_set_string (value, priv->physical_id);
		break;
	case PROP_LOGICAL_ID:
		g_value_set_string (value, priv->logical_id);
		break;
	case PROP_BACKEND_ID:
		g_value_set_string (value, priv->backend_id);
		break;
	case PROP_QUIRKS:
		g_value_set_object (value, priv->quirks);
		break;
	case PROP_PROXY:
		g_value_set_object (value, priv->proxy);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
fu_device_set_property (GObject *object, guint prop_id,
			const GValue *value, GParamSpec *pspec)
{
	FuDevice *self = FU_DEVICE (object);
	switch (prop_id) {
	case PROP_PROGRESS:
		fu_device_set_progress (self, g_value_get_uint (value));
		break;
	case PROP_PHYSICAL_ID:
		fu_device_set_physical_id (self, g_value_get_string (value));
		break;
	case PROP_LOGICAL_ID:
		fu_device_set_logical_id (self, g_value_get_string (value));
		break;
	case PROP_BACKEND_ID:
		fu_device_set_backend_id (self, g_value_get_string (value));
		break;
	case PROP_QUIRKS:
		fu_device_set_quirks (self, g_value_get_object (value));
		break;
	case PROP_PROXY:
		fu_device_set_proxy (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * fu_device_internal_flag_to_string:
 * @flag: A #FuDeviceInternalFlags, e.g. %FU_DEVICE_INTERNAL_FLAG_MD_SET_ICON
 *
 * Converts a #FuDeviceInternalFlags to a string.
 *
 * Return value: identifier string
 *
 * Since: 1.5.5
 **/
const gchar *
fu_device_internal_flag_to_string (FuDeviceInternalFlags flag)
{
	if (flag == FU_DEVICE_INTERNAL_FLAG_MD_SET_ICON)
		return "md-set-icon";
	if (flag == FU_DEVICE_INTERNAL_FLAG_MD_SET_NAME)
		return "md-set-name";
	if (flag == FU_DEVICE_INTERNAL_FLAG_MD_SET_NAME_CATEGORY)
		return "md-set-name-category";
	if (flag == FU_DEVICE_INTERNAL_FLAG_MD_SET_VERFMT)
		return "md-set-verfmt";
	if (flag == FU_DEVICE_INTERNAL_FLAG_ONLY_SUPPORTED)
		return "only-supported";
	if (flag == FU_DEVICE_INTERNAL_FLAG_NO_AUTO_INSTANCE_IDS)
		return "no-auto-instance-ids";
	if (flag == FU_DEVICE_INTERNAL_FLAG_ENSURE_SEMVER)
		return "ensure-semver";
	if (flag == FU_DEVICE_INTERNAL_FLAG_RETRY_OPEN)
		return "retry-open";
	if (flag == FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID)
		return "replug-match-guid";
	return NULL;
}

/**
 * fu_device_internal_flag_from_string:
 * @flag: A string, e.g. `md-set-icon`
 *
 * Converts a string to a #FuDeviceInternalFlags.
 *
 * Return value: enumerated value
 *
 * Since: 1.5.5
 **/
FuDeviceInternalFlags
fu_device_internal_flag_from_string (const gchar *flag)
{
	if (g_strcmp0 (flag, "md-set-icon") == 0)
		return FU_DEVICE_INTERNAL_FLAG_MD_SET_ICON;
	if (g_strcmp0 (flag, "md-set-name") == 0)
		return FU_DEVICE_INTERNAL_FLAG_MD_SET_NAME;
	if (g_strcmp0 (flag, "md-set-name-category") == 0)
		return FU_DEVICE_INTERNAL_FLAG_MD_SET_NAME_CATEGORY;
	if (g_strcmp0 (flag, "md-set-verfmt") == 0)
		return FU_DEVICE_INTERNAL_FLAG_MD_SET_VERFMT;
	if (g_strcmp0 (flag, "only-supported") == 0)
		return FU_DEVICE_INTERNAL_FLAG_ONLY_SUPPORTED;
	if (g_strcmp0 (flag, "no-auto-instance-ids") == 0)
		return FU_DEVICE_INTERNAL_FLAG_NO_AUTO_INSTANCE_IDS;
	if (g_strcmp0 (flag, "ensure-semver") == 0)
		return FU_DEVICE_INTERNAL_FLAG_ENSURE_SEMVER;
	if (g_strcmp0 (flag, "retry-open") == 0)
		return FU_DEVICE_INTERNAL_FLAG_RETRY_OPEN;
	return FU_DEVICE_INTERNAL_FLAG_UNKNOWN;
}

/**
 * fu_device_add_internal_flag:
 * @self: A #FuDevice
 * @flag: A #FuDeviceInternalFlags, e.g. %FU_DEVICE_INTERNAL_FLAG_MD_SET_ICON
 *
 * Adds a private flag that stays internal to the engine and is not leaked to the client.
 *
 * Since: 1.5.5
 **/
void
fu_device_add_internal_flag (FuDevice *self, FuDeviceInternalFlags flag)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));
	priv->internal_flags |= flag;
}

/**
 * fu_device_remove_internal_flag:
 * @self: A #FuDevice
 * @flag: A #FuDeviceInternalFlags, e.g. %FU_DEVICE_INTERNAL_FLAG_MD_SET_ICON
 *
 * Removes a private flag that stays internal to the engine and is not leaked to the client.
 *
 * Since: 1.5.5
 **/
void
fu_device_remove_internal_flag (FuDevice *self, FuDeviceInternalFlags flag)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));
	priv->internal_flags &= ~flag;
}

/**
 * fu_device_has_internal_flag:
 * @self: A #FuDevice
 * @flag: A #FuDeviceInternalFlags, e.g. %FU_DEVICE_INTERNAL_FLAG_MD_SET_ICON
 *
 * Tests for a private flag that stays internal to the engine and is not leaked to the client.
 *
 * Since: 1.5.5
 **/
gboolean
fu_device_has_internal_flag (FuDevice *self, FuDeviceInternalFlags flag)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	return (priv->internal_flags & flag) > 0;
}

/**
 * fu_device_get_possible_plugins:
 * @self: A #FuDevice
 *
 * Gets the list of possible plugin names, typically added from quirk files.
 *
 * Returns: (element-type utf8) (transfer container): plugin names
 *
 * Since: 1.3.3
 **/
GPtrArray *
fu_device_get_possible_plugins (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	return g_ptr_array_ref (priv->possible_plugins);
}

/**
 * fu_device_add_possible_plugin:
 * @self: A #FuDevice
 * @plugin: A plugin name, e.g. `dfu`
 *
 * Adds a plugin name to the list of plugins that *might* be able to handle this
 * device. This is tyically called from a quirk handler.
 *
 * Duplicate plugin names are ignored.
 *
 * Since: 1.5.1
 **/
void
fu_device_add_possible_plugin (FuDevice *self, const gchar *plugin)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);

	g_return_if_fail (FU_IS_DEVICE (self));
	g_return_if_fail (plugin != NULL);

	/* add if it does not already exist */
#if GLIB_CHECK_VERSION(2,54,3)
	if (g_ptr_array_find_with_equal_func (priv->possible_plugins, plugin,
					      g_str_equal, NULL))
		return;
#endif
	g_ptr_array_add (priv->possible_plugins, g_strdup (plugin));
}

/**
 * fu_device_retry_add_recovery:
 * @self: A #FuDevice
 * @domain: A #GQuark, or %0 for all domains
 * @code: A #GError code
 * @func: (scope async) (nullable): A function to recover the device
 *
 * Sets the optional function to be called when fu_device_retry() fails, which
 * is possibly a device reset.
 *
 * If @func is %NULL then recovery is not possible and an error is returned
 * straight away.
 *
 * Since: 1.4.0
 **/
void
fu_device_retry_add_recovery (FuDevice *self,
			      GQuark domain,
			      gint code,
			      FuDeviceRetryFunc func)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	FuDeviceRetryRecovery *rec;

	g_return_if_fail (FU_IS_DEVICE (self));
	g_return_if_fail (domain != 0);

	rec = g_new (FuDeviceRetryRecovery, 1);
	rec->domain = domain;
	rec->code = code;
	rec->recovery_func = func;
	g_ptr_array_add (priv->retry_recs, rec);
}

/**
 * fu_device_retry_set_delay:
 * @self: A #FuDevice
 * @delay: delay in ms
 *
 * Sets the recovery delay between failed retries.
 *
 * Since: 1.4.0
 **/
void
fu_device_retry_set_delay (FuDevice *self, guint delay)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));
	priv->retry_delay = delay;
}

/**
 * fu_device_retry_full:
 * @self: A #FuDevice
 * @func: (scope async): A function to execute
 * @count: The number of tries to try the function
 * @delay: The delay between each try in ms
 * @user_data: (nullable): a helper to pass to @user_data
 * @error: A #GError
 *
 * Calls a specific function a number of times, optionally handling the error
 * with a reset action.
 *
 * If fu_device_retry_add_recovery() has not been used then all errors are
 * considered non-fatal until the last try.
 *
 * If the reset function returns %FALSE, then the function returns straight away
 * without processing any pending retries.
 *
 * Since: 1.5.5
 **/
gboolean
fu_device_retry_full (FuDevice *self,
		      FuDeviceRetryFunc func,
		      guint count,
		      guint delay,
		      gpointer user_data,
		      GError **error)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (func != NULL, FALSE);
	g_return_val_if_fail (count >= 1, FALSE);
	g_return_val_if_fail (error != NULL, FALSE);

	for (guint i = 0; ; i++) {
		g_autoptr(GError) error_local =	NULL;

		/* delay */
		if (i > 0 && delay > 0)
			g_usleep (delay * 1000);

		/* run function, if success return success */
		if (func (self, user_data, &error_local))
			break;

		/* sanity check */
		if (error_local == NULL) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "exec failed but no error set!");
			return FALSE;
		}

		/* too many retries */
		if (i >= count - 1) {
			g_propagate_prefixed_error (error,
						    g_steal_pointer (&error_local),
						    "failed after %u retries: ",
						    count);
			return FALSE;
		}

		/* show recoverable error on the console */
		if (priv->retry_recs->len == 0) {
			g_debug ("failed on try %u of %u: %s",
				 i + 1, count, error_local->message);
			continue;
		}

		/* find the condition that matches */
		for (guint j = 0; j < priv->retry_recs->len; j++) {
			FuDeviceRetryRecovery *rec = g_ptr_array_index (priv->retry_recs, j);
			if (g_error_matches (error_local, rec->domain, rec->code)) {
				if (rec->recovery_func != NULL) {
					if (!rec->recovery_func (self, user_data, error))
						return FALSE;
				} else {
					g_set_error (error,
						     G_IO_ERROR,
						     G_IO_ERROR_FAILED,
						     "device recovery not possible");
					return FALSE;
				}
			}
		}
	}

	/* success */
	return TRUE;
}

/**
 * fu_device_retry:
 * @self: A #FuDevice
 * @func: (scope async): A function to execute
 * @count: The number of tries to try the function
 * @user_data: (nullable): a helper to pass to @user_data
 * @error: A #GError
 *
 * Calls a specific function a number of times, optionally handling the error
 * with a reset action.
 *
 * If fu_device_retry_add_recovery() has not been used then all errors are
 * considered non-fatal until the last try.
 *
 * If the reset function returns %FALSE, then the function returns straight away
 * without processing any pending retries.
 *
 * Since: 1.4.0
 **/
gboolean
fu_device_retry (FuDevice *self,
		 FuDeviceRetryFunc func,
		 guint count,
		 gpointer user_data,
		 GError **error)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	return fu_device_retry_full (self, func, count,
				     priv->retry_delay,
				     user_data, error);
}

/**
 * fu_device_poll:
 * @self: A #FuDevice
 * @error: A #GError, or %NULL
 *
 * Polls a device, typically querying the hardware for status.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.1.2
 **/
gboolean
fu_device_poll (FuDevice *self, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* subclassed */
	if (klass->poll != NULL) {
		if (!klass->poll (self, error))
			return FALSE;
	}
	return TRUE;
}

static gboolean
fu_device_poll_cb (gpointer user_data)
{
	FuDevice *self = FU_DEVICE (user_data);
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GError) error_local = NULL;
	if (!fu_device_poll (self, &error_local)) {
		g_warning ("disabling polling: %s", error_local->message);
		priv->poll_id = 0;
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

/**
 * fu_device_set_poll_interval:
 * @self: a #FuPlugin
 * @interval: duration in ms, or 0 to disable
 *
 * Polls the hardware every interval period. If the subclassed `->poll()` method
 * returns %FALSE then a warning is printed to the console and the poll is
 * disabled until the next call to fu_device_set_poll_interval().
 *
 * Since: 1.1.2
 **/
void
fu_device_set_poll_interval (FuDevice *self, guint interval)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);

	g_return_if_fail (FU_IS_DEVICE (self));

	if (priv->poll_id != 0) {
		g_source_remove (priv->poll_id);
		priv->poll_id = 0;
	}
	if (interval == 0)
		return;
	if (interval % 1000 == 0) {
		priv->poll_id = g_timeout_add_seconds (interval / 1000,
						       fu_device_poll_cb,
						       self);
	} else {
		priv->poll_id = g_timeout_add (interval, fu_device_poll_cb, self);
	}
}

/**
 * fu_device_get_order:
 * @self: a #FuPlugin
 *
 * Gets the device order, where higher numbers are installed after lower
 * numbers.
 *
 * Returns: the integer value
 *
 * Since: 1.0.8
 **/
gint
fu_device_get_order (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_DEVICE (self), 0);
	return priv->order;
}

/**
 * fu_device_set_order:
 * @self: a #FuDevice
 * @order: a integer value
 *
 * Sets the device order, where higher numbers are installed after lower
 * numbers.
 *
 * Since: 1.0.8
 **/
void
fu_device_set_order (FuDevice *self, gint order)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));
	priv->order = order;
}

/**
 * fu_device_get_priority:
 * @self: a #FuPlugin
 *
 * Gets the device priority, where higher numbers are better.
 *
 * Returns: the integer value
 *
 * Since: 1.1.1
 **/
guint
fu_device_get_priority (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_DEVICE (self), 0);
	return priv->priority;
}

/**
 * fu_device_set_priority:
 * @self: a #FuDevice
 * @priority: a integer value
 *
 * Sets the device priority, where higher numbers are better.
 *
 * Since: 1.1.1
 **/
void
fu_device_set_priority (FuDevice *self, guint priority)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));
	priv->priority = priority;
}

/**
 * fu_device_get_equivalent_id:
 * @self: A #FuDevice
 *
 * Gets any equivalent ID for a device
 *
 * Returns: (transfer none): a #gchar or NULL
 *
 * Since: 0.6.1
 **/
const gchar *
fu_device_get_equivalent_id (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	return priv->equivalent_id;
}

/**
 * fu_device_set_equivalent_id:
 * @self: A #FuDevice
 * @equivalent_id: A string
 *
 * Sets any equivalent ID for a device
 *
 * Since: 0.6.1
 **/
void
fu_device_set_equivalent_id (FuDevice *self, const gchar *equivalent_id)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));

	/* not changed */
	if (g_strcmp0 (priv->equivalent_id, equivalent_id) == 0)
		return;

	g_free (priv->equivalent_id);
	priv->equivalent_id = g_strdup (equivalent_id);
}

/**
 * fu_device_get_alternate_id:
 * @self: A #FuDevice
 *
 * Gets any alternate device ID. An alternate device may be linked to the primary
 * device in some way.
 *
 * Returns: (transfer none): a #FuDevice or %NULL
 *
 * Since: 1.1.0
 **/
const gchar *
fu_device_get_alternate_id (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	return priv->alternate_id;
}

/**
 * fu_device_set_alternate_id:
 * @self: A #FuDevice
 * @alternate_id: Another #FuDevice
 *
 * Sets any alternate device ID. An alternate device may be linked to the primary
 * device in some way.
 *
 * Since: 1.1.0
 **/
void
fu_device_set_alternate_id (FuDevice *self, const gchar *alternate_id)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));

	/* not changed */
	if (g_strcmp0 (priv->alternate_id, alternate_id) == 0)
		return;

	g_free (priv->alternate_id);
	priv->alternate_id = g_strdup (alternate_id);
}

/**
 * fu_device_get_alternate:
 * @self: A #FuDevice
 *
 * Gets any alternate device. An alternate device may be linked to the primary
 * device in some way.
 *
 * The alternate object will be matched from the ID set in fu_device_set_alternate_id()
 * and will be assigned by the daemon. This means if the ID is not found as an
 * added device, then this function will return %NULL.
 *
 * Returns: (transfer none): a #FuDevice or %NULL
 *
 * Since: 0.7.2
 **/
FuDevice *
fu_device_get_alternate (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	return priv->alternate;
}

/**
 * fu_device_set_alternate:
 * @self: A #FuDevice
 * @alternate: Another #FuDevice
 *
 * Sets any alternate device. An alternate device may be linked to the primary
 * device in some way.
 *
 * This function is only usable by the daemon, not directly from plugins.
 *
 * Since: 0.7.2
 **/
void
fu_device_set_alternate (FuDevice *self, FuDevice *alternate)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));
	g_set_object (&priv->alternate, alternate);
}

/**
 * fu_device_get_parent:
 * @self: A #FuDevice
 *
 * Gets any parent device. An parent device is logically "above" the current
 * device and this may be reflected in client tools.
 *
 * This information also allows the plugin to optionally verify the parent
 * device, for instance checking the parent device firmware version.
 *
 * The parent object is not refcounted and if destroyed this function will then
 * return %NULL.
 *
 * Returns: (transfer none): a #FuDevice or %NULL
 *
 * Since: 1.0.8
 **/
FuDevice *
fu_device_get_parent (FuDevice *self)
{
	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	return FU_DEVICE (fwupd_device_get_parent (FWUPD_DEVICE (self)));
}

/**
 * fu_device_get_root:
 * @self: A #FuDevice
 *
 * Gets the root parent device. A parent device is logically "above" the current
 * device and this may be reflected in client tools.
 *
 * If there is no parent device defined, then @self is returned.
 *
 * Returns: (transfer full): a #FuDevice
 *
 * Since: 1.4.0
 **/
FuDevice *
fu_device_get_root (FuDevice *self)
{
	FuDevice *parent;
	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	do {
		parent = fu_device_get_parent (self);
		if (parent != NULL)
			self = parent;
	} while (parent != NULL);
	return g_object_ref (self);
}

/**
 * fu_device_set_parent:
 * @self: A #FuDevice
 * @parent: A #FuDevice
 *
 * Sets any parent device. An parent device is logically "above" the current
 * device and this may be reflected in client tools.
 *
 * This information also allows the plugin to optionally verify the parent
 * device, for instance checking the parent device firmware version.
 *
 * Since: 1.0.8
 **/
void
fu_device_set_parent (FuDevice *self, FuDevice *parent)
{
	g_return_if_fail (FU_IS_DEVICE (self));

	/* if the parent has quirks, make the child inherit it */
	if (parent != NULL) {
		if (fu_device_get_quirks (self) == NULL &&
		    fu_device_get_quirks (parent) != NULL)
			fu_device_set_quirks (self, fu_device_get_quirks (parent));
	}

	fwupd_device_set_parent (FWUPD_DEVICE (self), FWUPD_DEVICE (parent));
}

/**
 * fu_device_set_proxy:
 * @self: A #FuDevice
 * @proxy: A #FuDevice
 *
 * Sets any proxy device. A proxy device can be used to perform an action on
 * behalf of another device, for instance attach()ing it after a successful
 * update.
 *
 * Since: 1.4.1
 **/
void
fu_device_set_proxy (FuDevice *self, FuDevice *proxy)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);

	g_return_if_fail (FU_IS_DEVICE (self));

	if (priv->proxy != NULL)
		g_object_remove_weak_pointer (G_OBJECT (priv->proxy), (gpointer *) &priv->proxy);
	if (proxy != NULL)
		g_object_add_weak_pointer (G_OBJECT (proxy), (gpointer *) &priv->proxy);
	priv->proxy = proxy;
}

/**
 * fu_device_get_proxy:
 * @self: A #FuDevice
 *
 * Gets any proxy device. A proxy device can be used to perform an action on
 * behalf of another device, for instance attach()ing it after a successful
 * update.
 *
 * The proxy object is not refcounted and if destroyed this function will then
 * return %NULL.
 *
 * Returns: (transfer none): a #FuDevice or %NULL
 *
 * Since: 1.4.1
 **/
FuDevice *
fu_device_get_proxy (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	return priv->proxy;
}

/**
 * fu_device_get_children:
 * @self: A #FuDevice
 *
 * Gets any child devices. A child device is logically "below" the current
 * device and this may be reflected in client tools.
 *
 * Returns: (transfer none) (element-type FuDevice): child devices
 *
 * Since: 1.0.8
 **/
GPtrArray *
fu_device_get_children (FuDevice *self)
{
	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	return fwupd_device_get_children (FWUPD_DEVICE (self));
}

/**
 * fu_device_add_child:
 * @self: A #FuDevice
 * @child: Another #FuDevice
 *
 * Sets any child device. An child device is logically linked to the primary
 * device in some way.
 *
 * Since: 1.0.8
 **/
void
fu_device_add_child (FuDevice *self, FuDevice *child)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	GPtrArray *children;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (FU_IS_DEVICE (self));
	g_return_if_fail (FU_IS_DEVICE (child));

	/* add if the child does not already exist */
	fwupd_device_add_child (FWUPD_DEVICE (self), FWUPD_DEVICE (child));

	/* ensure the parent has the MAX() of the childrens removal delay  */
	children = fu_device_get_children (self);
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child_tmp = g_ptr_array_index (children, i);
		guint remove_delay = fu_device_get_remove_delay (child_tmp);
		if (remove_delay > priv->remove_delay) {
			g_debug ("setting remove delay to %u as child is greater than %u",
				 remove_delay, priv->remove_delay);
			priv->remove_delay = remove_delay;
		}
	}

	/* copy from main device if unset */
	if (fu_device_get_physical_id (child) == NULL &&
	    fu_device_get_physical_id (self) != NULL)
		fu_device_set_physical_id (child, fu_device_get_physical_id (self));
	if (fu_device_get_vendor (child) == NULL)
		fu_device_set_vendor (child, fu_device_get_vendor (self));
	if (fu_device_get_vendor_ids(child)->len == 0) {
		GPtrArray *vendor_ids = fu_device_get_vendor_ids (self);
		for (guint i = 0; i < vendor_ids->len; i++) {
			const gchar *vendor_id = g_ptr_array_index (vendor_ids, i);
			fu_device_add_vendor_id (child, vendor_id);
		}
	}
	if (fu_device_get_icons(child)->len == 0) {
		GPtrArray *icons = fu_device_get_icons (self);
		for (guint i = 0; i < icons->len; i++) {
			const gchar *icon_name = g_ptr_array_index (icons, i);
			fu_device_add_icon (child, icon_name);
		}
	}

	/* ensure the ID is converted */
	if (!fu_device_ensure_id (child, &error))
		g_warning ("failed to ensure child: %s", error->message);

	/* ensure the parent is also set on the child */
	fu_device_set_parent (child, self);
}

/**
 * fu_device_get_parent_guids:
 * @self: A #FuDevice
 *
 * Gets any parent device GUIDs. If a device is added to the daemon that matches
 * any GUIDs added from fu_device_add_parent_guid() then this device is marked the parent of @self.
 *
 * Returns: (transfer none) (element-type utf8): a list of GUIDs
 *
 * Since: 1.0.8
 **/
GPtrArray *
fu_device_get_parent_guids (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GRWLockReaderLocker) locker = g_rw_lock_reader_locker_new (&priv->parent_guids_mutex);
	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	g_return_val_if_fail (locker != NULL, NULL);
	return priv->parent_guids;
}

/**
 * fu_device_has_parent_guid:
 * @self: A #FuDevice
 * @guid: a GUID
 *
 * Searches the list of parent GUIDs for a string match.
 *
 * Returns: %TRUE if the parent GUID exists
 *
 * Since: 1.0.8
 **/
gboolean
fu_device_has_parent_guid (FuDevice *self, const gchar *guid)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GRWLockReaderLocker) locker = g_rw_lock_reader_locker_new (&priv->parent_guids_mutex);
	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (locker != NULL, FALSE);
	for (guint i = 0; i < priv->parent_guids->len; i++) {
		const gchar *guid_tmp = g_ptr_array_index (priv->parent_guids, i);
		if (g_strcmp0 (guid_tmp, guid) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fu_device_add_parent_guid:
 * @self: A #FuDevice
 * @guid: a GUID
 *
 * Sets any parent device using a GUID. An parent device is logically linked to
 * the primary device in some way and can be added before or after @self.
 *
 * The GUIDs are searched in order, and so the order of adding GUIDs may be
 * important if more than one parent device might match.
 *
 * If the parent device is removed, any children logically linked to it will
 * also be removed.
 *
 * Since: 1.0.8
 **/
void
fu_device_add_parent_guid (FuDevice *self, const gchar *guid)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GRWLockWriterLocker) locker = NULL;

	g_return_if_fail (FU_IS_DEVICE (self));
	g_return_if_fail (guid != NULL);

	/* make valid */
	if (!fwupd_guid_is_valid (guid)) {
		g_autofree gchar *tmp = fwupd_guid_hash_string (guid);
		if (fu_device_has_parent_guid (self, tmp))
			return;
		g_debug ("using %s for %s", tmp, guid);
		g_ptr_array_add (priv->parent_guids, g_steal_pointer (&tmp));
		return;
	}

	/* already valid */
	if (fu_device_has_parent_guid (self, guid))
		return;
	locker = g_rw_lock_writer_locker_new (&priv->parent_guids_mutex);
	g_return_if_fail (locker != NULL);
	g_ptr_array_add (priv->parent_guids, g_strdup (guid));
}

static gboolean
fu_device_add_child_by_type_guid (FuDevice *self,
				  GType type,
				  const gchar *guid,
				  GError **error)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(FuDevice) child = NULL;
	child = g_object_new (type,
			      "quirks", priv->quirks,
			      "logical-id", guid,
			      NULL);
	fu_device_add_guid (child, guid);
	if (fu_device_get_physical_id (self) != NULL)
		fu_device_set_physical_id (child, fu_device_get_physical_id (self));
	if (!fu_device_ensure_id (self, error))
		return FALSE;
	if (!fu_device_probe (child, error))
		return FALSE;
	fu_device_convert_instance_ids (child);
	fu_device_add_child (self, child);
	return TRUE;
}

static gboolean
fu_device_add_child_by_kv (FuDevice *self, const gchar *str, GError **error)
{
	g_auto(GStrv) split = g_strsplit (str, "|", -1);

	/* type same as parent */
	if (g_strv_length (split) == 1) {
		return fu_device_add_child_by_type_guid (self,
							 G_OBJECT_TYPE (self),
							 split[1],
							 error);
	}

	/* type specified */
	if (g_strv_length (split) == 2) {
		GType devtype = g_type_from_name (split[0]);
		if (devtype == 0) {
			g_set_error_literal (error,
					     G_IO_ERROR,
					     G_IO_ERROR_NOT_FOUND,
					     "no GType registered");
			return FALSE;
		}
		return fu_device_add_child_by_type_guid (self,
							 devtype,
							 split[1],
							 error);
	}

	/* more than one '|' */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_FOUND,
			     "unable to add parse child section");
	return FALSE;
}

static gboolean
fu_device_set_quirk_kv (FuDevice *self,
			const gchar *key,
			const gchar *value,
			GError **error)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);

	if (g_strcmp0 (key, FU_QUIRKS_PLUGIN) == 0) {
		fu_device_add_possible_plugin (self, value);
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_FLAGS) == 0) {
		fu_device_set_custom_flags (self, value);
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_NAME) == 0) {
		fu_device_set_name (self, value);
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_SUMMARY) == 0) {
		fu_device_set_summary (self, value);
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_BRANCH) == 0) {
		fu_device_set_branch (self, value);
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_VENDOR) == 0) {
		fu_device_set_vendor (self, value);
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_VENDOR_ID) == 0) {
		fu_device_add_vendor_id (self, value);
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_PROTOCOL) == 0) {
		fu_device_set_protocol (self, value);
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_VERSION) == 0) {
		fu_device_set_version (self, value);
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_UPDATE_MESSAGE) == 0) {
		fu_device_set_update_message (self, value);
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_UPDATE_IMAGE) == 0) {
		fu_device_set_update_image (self, value);
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_ICON) == 0) {
		fu_device_add_icon (self, value);
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_GUID) == 0) {
		fu_device_add_guid (self, value);
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_COUNTERPART_GUID) == 0) {
		fu_device_add_counterpart_guid (self, value);
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_PARENT_GUID) == 0) {
		fu_device_add_parent_guid (self, value);
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_PROXY_GUID) == 0) {
		fu_device_set_proxy_guid (self, value);
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_FIRMWARE_SIZE_MIN) == 0) {
		fu_device_set_firmware_size_min (self, fu_common_strtoull (value));
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_FIRMWARE_SIZE_MAX) == 0) {
		fu_device_set_firmware_size_max (self, fu_common_strtoull (value));
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_FIRMWARE_SIZE) == 0) {
		fu_device_set_firmware_size (self, fu_common_strtoull (value));
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_INSTALL_DURATION) == 0) {
		fu_device_set_install_duration (self, fu_common_strtoull (value));
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_PRIORITY) == 0) {
		fu_device_set_priority (self, fu_common_strtoull (value));
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_REMOVE_DELAY) == 0) {
		fu_device_set_remove_delay (self, fu_common_strtoull (value));
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_VERSION_FORMAT) == 0) {
		fu_device_set_version_format (self, fwupd_version_format_from_string (value));
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_GTYPE) == 0) {
		if (priv->specialized_gtype != G_TYPE_INVALID) {
			g_debug ("already set GType to %s, ignoring %s",
				 g_type_name (priv->specialized_gtype), value);
			return TRUE;
		}
		priv->specialized_gtype = g_type_from_name (value);
		if (priv->specialized_gtype == G_TYPE_INVALID) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_NOT_FOUND,
				     "device GType %s not supported", value);
			return FALSE;
		}
		return TRUE;
	}
	if (g_strcmp0 (key, FU_QUIRKS_CHILDREN) == 0) {
		g_auto(GStrv) sections = g_strsplit (value, ",", -1);
		for (guint i = 0; sections[i] != NULL; i++) {
			if (!fu_device_add_child_by_kv (self, sections[i], error))
				return FALSE;
		}
		return TRUE;
	}

	/* optional device-specific method */
	if (klass->set_quirk_kv != NULL)
		return klass->set_quirk_kv (self, key, value, error);

	/* failed */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "quirk key not supported");
	return FALSE;
}

/**
 * fu_device_get_specialized_gtype:
 * @self: A #FuDevice
 *
 * Gets the specialized type of the device
 *
 * Returns:#GType
 *
 * Since: 1.3.3
 **/
GType
fu_device_get_specialized_gtype (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	return priv->specialized_gtype;
}

static void
fu_device_quirks_iter_cb (FuQuirks *quirks, const gchar *key, const gchar *value, gpointer user_data)
{
	FuDevice *self = FU_DEVICE (user_data);
	g_autoptr(GError) error = NULL;
	if (!fu_device_set_quirk_kv (self, key, value, &error)) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED)) {
			g_warning ("failed to set quirk key %s=%s: %s",
				   key, value, error->message);
		}
	}
}

static void
fu_device_add_guid_quirks (FuDevice *self, const gchar *guid)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	if (priv->quirks == NULL)
		return;
	fu_quirks_lookup_by_id_iter (priv->quirks, guid, fu_device_quirks_iter_cb, self);
}

/**
 * fu_device_set_firmware_size:
 * @self: A #FuDevice
 * @size: Size in bytes
 *
 * Sets the exact allowed size of the firmware blob.
 *
 * Since: 1.2.6
 **/
void
fu_device_set_firmware_size (FuDevice *self, guint64 size)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));
	priv->size_min = size;
	priv->size_max = size;
}

/**
 * fu_device_set_firmware_size_min:
 * @self: A #FuDevice
 * @size_min: Size in bytes
 *
 * Sets the minimum allowed size of the firmware blob.
 *
 * Since: 1.1.2
 **/
void
fu_device_set_firmware_size_min (FuDevice *self, guint64 size_min)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));
	priv->size_min = size_min;
}

/**
 * fu_device_set_firmware_size_max:
 * @self: A #FuDevice
 * @size_max: Size in bytes
 *
 * Sets the maximum allowed size of the firmware blob.
 *
 * Since: 1.1.2
 **/
void
fu_device_set_firmware_size_max (FuDevice *self, guint64 size_max)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));
	priv->size_max = size_max;
}

/**
 * fu_device_get_firmware_size_min:
 * @self: A #FuDevice
 *
 * Gets the minimum size of the firmware blob.
 *
 * Returns: Size in bytes, or 0 if unset
 *
 * Since: 1.2.6
 **/
guint64
fu_device_get_firmware_size_min (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_DEVICE (self), 0);
	return priv->size_min;
}

/**
 * fu_device_get_firmware_size_max:
 * @self: A #FuDevice
 *
 * Gets the maximum size of the firmware blob.
 *
 * Returns: Size in bytes, or 0 if unset
 *
 * Since: 1.2.6
 **/
guint64
fu_device_get_firmware_size_max (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_DEVICE (self), 0);
	return priv->size_max;
}

static void
fu_device_add_guid_safe (FuDevice *self, const gchar *guid)
{
	/* add the device GUID before adding additional GUIDs from quirks
	 * to ensure the bootloader GUID is listed after the runtime GUID */
	fwupd_device_add_guid (FWUPD_DEVICE (self), guid);
	fu_device_add_guid_quirks (self, guid);
}

/**
 * fu_device_has_guid:
 * @self: A #FuDevice
 * @guid: A GUID, e.g. `WacomAES`
 *
 * Finds out if the device has a specific GUID.
 *
 * Returns: %TRUE if the GUID is found
 *
 * Since: 1.2.2
 **/
gboolean
fu_device_has_guid (FuDevice *self, const gchar *guid)
{
	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (guid != NULL, FALSE);

	/* make valid */
	if (!fwupd_guid_is_valid (guid)) {
		g_autofree gchar *tmp = fwupd_guid_hash_string (guid);
		return fwupd_device_has_guid (FWUPD_DEVICE (self), tmp);
	}

	/* already valid */
	return fwupd_device_has_guid (FWUPD_DEVICE (self), guid);
}

/**
 * fu_device_add_instance_id_full:
 * @self: A #FuDevice
 * @instance_id: A Instance ID, e.g. `WacomAES`
 * @flags: A #FuDeviceInstanceFlags
 *
 * Adds an instance ID with all parameters set
 *
 *
 * Since: 1.2.9
 **/
void
fu_device_add_instance_id_full (FuDevice *self,
				const gchar *instance_id,
				FuDeviceInstanceFlags flags)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_autofree gchar *guid = NULL;
	if (fwupd_guid_is_valid (instance_id)) {
		g_warning ("use fu_device_add_guid(\"%s\") instead!", instance_id);
		fu_device_add_guid_safe (self, instance_id);
		return;
	}

	/* it seems odd adding the instance ID and the GUID quirks and not just
	 * calling fu_device_add_guid_safe() -- but we want the quirks to match
	 * so the plugin is set, but not the LVFS metadata to match firmware
	 * until we're sure the device isn't using _NO_AUTO_INSTANCE_IDS */
	guid = fwupd_guid_hash_string (instance_id);
	fu_device_add_guid_quirks (self, guid);
	if ((flags & FU_DEVICE_INSTANCE_FLAG_ONLY_QUIRKS) == 0)
		fwupd_device_add_instance_id (FWUPD_DEVICE (self), instance_id);

	/* already done by ->setup(), so this must be ->registered() */
	if (priv->done_setup)
		fwupd_device_add_guid (FWUPD_DEVICE (self), guid);
}

/**
 * fu_device_add_instance_id:
 * @self: A #FuDevice
 * @instance_id: the InstanceID, e.g. `PCI\VEN_10EC&DEV_525A`
 *
 * Adds an instance ID to the device. If the @instance_id argument is already a
 * valid GUID then fu_device_add_guid() should be used instead.
 *
 * Since: 1.2.5
 **/
void
fu_device_add_instance_id (FuDevice *self, const gchar *instance_id)
{
	g_return_if_fail (FU_IS_DEVICE (self));
	g_return_if_fail (instance_id != NULL);
	fu_device_add_instance_id_full (self, instance_id, FU_DEVICE_INSTANCE_FLAG_NONE);
}

/**
 * fu_device_add_guid:
 * @self: A #FuDevice
 * @guid: A GUID, e.g. `2082b5e0-7a64-478a-b1b2-e3404fab6dad`
 *
 * Adds a GUID to the device. If the @guid argument is not a valid GUID then it
 * is converted to a GUID using fwupd_guid_hash_string().
 *
 * Since: 0.7.2
 **/
void
fu_device_add_guid (FuDevice *self, const gchar *guid)
{
	g_return_if_fail (FU_IS_DEVICE (self));
	g_return_if_fail (guid != NULL);
	if (!fwupd_guid_is_valid (guid)) {
		fu_device_add_instance_id (self, guid);
		return;
	}
	fu_device_add_guid_safe (self, guid);
}

/**
 * fu_device_add_counterpart_guid:
 * @self: A #FuDevice
 * @guid: A GUID, e.g. `2082b5e0-7a64-478a-b1b2-e3404fab6dad`
 *
 * Adds a GUID to the device. If the @guid argument is not a valid GUID then it
 * is converted to a GUID using fwupd_guid_hash_string().
 *
 * A counterpart GUID is typically the GUID of the same device in bootloader
 * or runtime mode, if they have a different device PCI or USB ID. Adding this
 * type of GUID does not cause a "cascade" by matching using the quirk database.
 *
 * Since: 1.1.2
 **/
void
fu_device_add_counterpart_guid (FuDevice *self, const gchar *guid)
{
	g_return_if_fail (FU_IS_DEVICE (self));
	g_return_if_fail (guid != NULL);

	/* make valid */
	if (!fwupd_guid_is_valid (guid)) {
		g_autofree gchar *tmp = fwupd_guid_hash_string (guid);
		fwupd_device_add_guid (FWUPD_DEVICE (self), tmp);
		return;
	}

	/* already valid */
	fwupd_device_add_guid (FWUPD_DEVICE (self), guid);
}

/**
 * fu_device_get_guids_as_str:
 * @self: A #FuDevice
 *
 * Gets the device GUIDs as a joined string, which may be useful for error
 * messages.
 *
 * Returns: a string, which may be empty length but not %NULL
 *
 * Since: 1.0.8
 **/
gchar *
fu_device_get_guids_as_str (FuDevice *self)
{
	GPtrArray *guids;
	g_autofree gchar **tmp = NULL;

	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);

	guids = fu_device_get_guids (self);
	tmp = g_new0 (gchar *, guids->len + 1);
	for (guint i = 0; i < guids->len; i++)
		tmp[i] = g_ptr_array_index (guids, i);
	return g_strjoinv (",", tmp);
}

/**
 * fu_device_get_metadata:
 * @self: A #FuDevice
 * @key: the key
 *
 * Gets an item of metadata from the device.
 *
 * Returns: a string value, or %NULL for unfound.
 *
 * Since: 0.1.0
 **/
const gchar *
fu_device_get_metadata (FuDevice *self, const gchar *key)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GRWLockReaderLocker) locker = g_rw_lock_reader_locker_new (&priv->metadata_mutex);
	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	g_return_val_if_fail (locker != NULL, NULL);
	if (priv->metadata == NULL)
		return NULL;
	return g_hash_table_lookup (priv->metadata, key);
}

/**
 * fu_device_get_metadata_boolean:
 * @self: A #FuDevice
 * @key: the key
 *
 * Gets an item of metadata from the device.
 *
 * Returns: a boolean value, or %FALSE for unfound or failure to parse.
 *
 * Since: 0.9.7
 **/
gboolean
fu_device_get_metadata_boolean (FuDevice *self, const gchar *key)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	const gchar *tmp;
	g_autoptr(GRWLockReaderLocker) locker = g_rw_lock_reader_locker_new (&priv->metadata_mutex);

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (key != NULL, FALSE);
	g_return_val_if_fail (locker != NULL, FALSE);

	if (priv->metadata == NULL)
		return FALSE;
	tmp = g_hash_table_lookup (priv->metadata, key);
	if (tmp == NULL)
		return FALSE;
	return g_strcmp0 (tmp, "true") == 0;
}

/**
 * fu_device_get_metadata_integer:
 * @self: A #FuDevice
 * @key: the key
 *
 * Gets an item of metadata from the device.
 *
 * Returns: a string value, or %G_MAXUINT for unfound or failure to parse.
 *
 * Since: 0.9.7
 **/
guint
fu_device_get_metadata_integer (FuDevice *self, const gchar *key)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	const gchar *tmp;
	gchar *endptr = NULL;
	guint64 val;
	g_autoptr(GRWLockReaderLocker) locker = g_rw_lock_reader_locker_new (&priv->metadata_mutex);

	g_return_val_if_fail (FU_IS_DEVICE (self), G_MAXUINT);
	g_return_val_if_fail (key != NULL, G_MAXUINT);
	g_return_val_if_fail (locker != NULL, G_MAXUINT);

	if (priv->metadata == NULL)
		return G_MAXUINT;
	tmp = g_hash_table_lookup (priv->metadata, key);
	if (tmp == NULL)
		return G_MAXUINT;
	val = g_ascii_strtoull (tmp, &endptr, 10);
	if (endptr != NULL && endptr[0] != '\0')
		return G_MAXUINT;
	if (val > G_MAXUINT)
		return G_MAXUINT;
	return (guint) val;
}

/**
 * fu_device_remove_metadata:
 * @self: A #FuDevice
 * @key: the key
 *
 * Removes an item of metadata on the device.
 *
 * Since: 1.3.3
 **/
void
fu_device_remove_metadata (FuDevice *self, const gchar *key)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GRWLockWriterLocker) locker = g_rw_lock_writer_locker_new (&priv->metadata_mutex);
	g_return_if_fail (FU_IS_DEVICE (self));
	g_return_if_fail (key != NULL);
	g_return_if_fail (locker != NULL);
	if (priv->metadata == NULL)
		return;
	g_hash_table_remove (priv->metadata, key);
}

/**
 * fu_device_set_metadata:
 * @self: A #FuDevice
 * @key: the key
 * @value: the string value
 *
 * Sets an item of metadata on the device.
 *
 * Since: 0.1.0
 **/
void
fu_device_set_metadata (FuDevice *self, const gchar *key, const gchar *value)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(GRWLockWriterLocker) locker = g_rw_lock_writer_locker_new (&priv->metadata_mutex);
	g_return_if_fail (FU_IS_DEVICE (self));
	g_return_if_fail (key != NULL);
	g_return_if_fail (value != NULL);
	g_return_if_fail (locker != NULL);
	if (priv->metadata == NULL) {
		priv->metadata = g_hash_table_new_full (g_str_hash, g_str_equal,
							g_free, g_free);
	}
	g_hash_table_insert (priv->metadata, g_strdup (key), g_strdup (value));
}

/**
 * fu_device_set_metadata_boolean:
 * @self: A #FuDevice
 * @key: the key
 * @value: the boolean value
 *
 * Sets an item of metadata on the device. When @value is set to %TRUE
 * the actual stored value is "true".
 *
 * Since: 0.9.7
 **/
void
fu_device_set_metadata_boolean (FuDevice *self, const gchar *key, gboolean value)
{
	g_return_if_fail (FU_IS_DEVICE (self));
	g_return_if_fail (key != NULL);

	fu_device_set_metadata (self, key, value ? "true" : "false");
}

/**
 * fu_device_set_metadata_integer:
 * @self: A #FuDevice
 * @key: the key
 * @value: the unsigned integer value
 *
 * Sets an item of metadata on the device. The integer is stored as a
 * base-10 string internally.
 *
 * Since: 0.9.7
 **/
void
fu_device_set_metadata_integer (FuDevice *self, const gchar *key, guint value)
{
	g_autofree gchar *tmp = g_strdup_printf ("%u", value);

	g_return_if_fail (FU_IS_DEVICE (self));
	g_return_if_fail (key != NULL);

	fu_device_set_metadata (self, key, tmp);
}

/**
 * fu_device_set_name:
 * @self: A #FuDevice
 * @value: a device name
 *
 * Sets the name on the device. Any invalid parts will be converted or removed.
 *
 * Since: 0.7.1
 **/
void
fu_device_set_name (FuDevice *self, const gchar *value)
{
	g_autoptr(GString) new = g_string_new (value);

	g_return_if_fail (FU_IS_DEVICE (self));
	g_return_if_fail (value != NULL);

	/* overwriting? */
	if (g_strcmp0 (value, fu_device_get_name (self)) == 0) {
		const gchar *id = fu_device_get_id (self);
		g_debug ("%s device overwriting same name value: %s",
			 id != NULL ? id : "unknown", value);
		return;
	}

	/* changing */
	if (fu_device_get_name (self) != NULL) {
		const gchar *id = fu_device_get_id (self);
		g_debug ("%s device overwriting name value: %s->%s",
			id != NULL ? id : "unknown",
			 fu_device_get_name (self),
			 value);
	}

	g_strdelimit (new->str, "_", ' ');
	fu_common_string_replace (new, "(TM)", "™");
	fwupd_device_set_name (FWUPD_DEVICE (self), new->str);
}

/**
 * fu_device_set_id:
 * @self: A #FuDevice
 * @id: a string, e.g. `tbt-port1`
 *
 * Sets the ID on the device. The ID should represent the *connection* of the
 * device, so that any similar device plugged into a different slot will
 * have a different @id string.
 *
 * The @id will be converted to a SHA1 hash if required before the device is
 * added to the daemon, and plugins should not assume that the ID that is set
 * here is the same as what is returned by fu_device_get_id().
 *
 * Since: 0.7.1
 **/
void
fu_device_set_id (FuDevice *self, const gchar *id)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	GPtrArray *children;
	g_autofree gchar *id_hash = NULL;

	g_return_if_fail (FU_IS_DEVICE (self));
	g_return_if_fail (id != NULL);

	/* allow sane device-id to be set directly */
	if (fwupd_device_id_is_valid (id)) {
		id_hash = g_strdup (id);
	} else {
		id_hash = g_compute_checksum_for_string (G_CHECKSUM_SHA1, id, -1);
		g_debug ("using %s for %s", id_hash, id);
	}
	fwupd_device_set_id (FWUPD_DEVICE (self), id_hash);
	priv->device_id_valid = TRUE;

	/* ensure the parent ID is set */
	children = fu_device_get_children (self);
	for (guint i = 0; i < children->len; i++) {
		FuDevice *devtmp = g_ptr_array_index (children, i);
		fwupd_device_set_parent_id (FWUPD_DEVICE (devtmp), id_hash);
	}
}

/**
 * fu_device_set_version_format:
 * @self: A #FwupdDevice
 * @fmt: the #FwupdVersionFormat, e.g. %FWUPD_VERSION_FORMAT_PLAIN
 *
 * Sets the version format.
 *
 * Since: 1.4.0
 **/
void
fu_device_set_version_format (FuDevice *self, FwupdVersionFormat fmt)
{
	/* same */
	if (fu_device_get_version_format (self) == fmt)
		return;
	if (fu_device_get_version_format (self) != FWUPD_VERSION_FORMAT_UNKNOWN) {
		g_debug ("changing verfmt for %s: %s->%s",
			 fu_device_get_id (self),
			 fwupd_version_format_to_string (fu_device_get_version_format (self)),
			 fwupd_version_format_to_string (fmt));
	}
	fwupd_device_set_version_format (FWUPD_DEVICE (self), fmt);
}

/**
 * fu_device_set_version:
 * @self: A #FuDevice
 * @version: (allow-none): a string, e.g. `1.2.3`
 *
 * Sets the device version, sanitizing the string if required.
 *
 * Since: 1.2.9
 **/
void
fu_device_set_version (FuDevice *self, const gchar *version)
{
	g_autofree gchar *version_safe = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (FU_IS_DEVICE (self));

	/* sanitize if required */
	if (fu_device_has_internal_flag (self, FU_DEVICE_INTERNAL_FLAG_ENSURE_SEMVER)) {
		version_safe = fu_common_version_ensure_semver (version);
		if (g_strcmp0 (version, version_safe) != 0)
			g_debug ("converted '%s' to '%s'", version, version_safe);
	} else {
		version_safe = g_strdup (version);
	}

	/* print a console warning for an invalid version, if semver */
	if (version_safe != NULL &&
	    !fu_common_version_verify_format (version_safe, fu_device_get_version_format (self), &error))
		g_warning ("%s", error->message);

	/* if different */
	if (g_strcmp0 (fu_device_get_version (self), version_safe) != 0) {
		if (fu_device_get_version (self) != NULL) {
			g_debug ("changing version for %s: %s->%s",
				 fu_device_get_id (self),
				 fu_device_get_version (self),
				 version_safe);
		}
		fwupd_device_set_version (FWUPD_DEVICE (self), version_safe);
	}
}

/**
 * fu_device_set_version_lowest:
 * @self: A #FuDevice
 * @version: (allow-none): a string, e.g. `1.2.3`
 *
 * Sets the device lowest version, sanitizing the string if required.
 *
 * Since: 1.4.0
 **/
void
fu_device_set_version_lowest (FuDevice *self, const gchar *version)
{
	g_autofree gchar *version_safe = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (FU_IS_DEVICE (self));

	/* sanitize if required */
	if (fu_device_has_internal_flag (self, FU_DEVICE_INTERNAL_FLAG_ENSURE_SEMVER)) {
		version_safe = fu_common_version_ensure_semver (version);
		if (g_strcmp0 (version, version_safe) != 0)
			g_debug ("converted '%s' to '%s'", version, version_safe);
	} else {
		version_safe = g_strdup (version);
	}

	/* print a console warning for an invalid version, if semver */
	if (version_safe != NULL &&
	    !fu_common_version_verify_format (version_safe, fu_device_get_version_format (self), &error))
		g_warning ("%s", error->message);

	/* if different */
	if (g_strcmp0 (fu_device_get_version_lowest (self), version_safe) != 0) {
		if (fu_device_get_version_lowest (self) != NULL) {
			g_debug ("changing version lowest for %s: %s->%s",
				 fu_device_get_id (self),
				 fu_device_get_version_lowest (self),
				 version_safe);
		}
		fwupd_device_set_version_lowest (FWUPD_DEVICE (self), version_safe);
	}
}

/**
 * fu_device_set_version_bootloader:
 * @self: A #FuDevice
 * @version: (allow-none): a string, e.g. `1.2.3`
 *
 * Sets the device bootloader version, sanitizing the string if required.
 *
 * Since: 1.4.0
 **/
void
fu_device_set_version_bootloader (FuDevice *self, const gchar *version)
{
	g_autofree gchar *version_safe = NULL;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (FU_IS_DEVICE (self));

	/* sanitize if required */
	if (fu_device_has_internal_flag (self, FU_DEVICE_INTERNAL_FLAG_ENSURE_SEMVER)) {
		version_safe = fu_common_version_ensure_semver (version);
		if (g_strcmp0 (version, version_safe) != 0)
			g_debug ("converted '%s' to '%s'", version, version_safe);
	} else {
		version_safe = g_strdup (version);
	}

	/* print a console warning for an invalid version, if semver */
	if (version_safe != NULL &&
	    !fu_common_version_verify_format (version_safe, fu_device_get_version_format (self), &error))
		g_warning ("%s", error->message);

	/* if different */
	if (g_strcmp0 (fu_device_get_version_bootloader (self), version_safe) != 0) {
		if (fu_device_get_version_bootloader (self) != NULL) {
			g_debug ("changing version for %s: %s->%s",
				 fu_device_get_id (self),
				 fu_device_get_version_bootloader (self),
				 version_safe);
		}
		fwupd_device_set_version_bootloader (FWUPD_DEVICE (self), version_safe);
	}
}

/**
 * fu_device_ensure_id:
 * @self: A #FuDevice
 * @error: A #GError
 *
 * If not already set, generates a device ID with the optional physical and
 * logical IDs.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.1.2
 **/
gboolean
fu_device_ensure_id (FuDevice *self, GError **error)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_autofree gchar *device_id = NULL;

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already set */
	if (priv->device_id_valid)
		return TRUE;

	/* nothing we can do! */
	if (priv->physical_id == NULL) {
		g_autofree gchar *tmp = fu_device_to_string (self);
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "cannot ensure ID: %s", tmp);
		return FALSE;
	}

	/* logical may be NULL */
	device_id = g_strjoin (":",
			       fu_device_get_physical_id (self),
			       fu_device_get_logical_id (self),
			       NULL);
	fu_device_set_id (self, device_id);
	return TRUE;
}

/**
 * fu_device_get_logical_id:
 * @self: A #FuDevice
 *
 * Gets the logical ID set for the device, which disambiguates devices with the
 * same physical ID.
 *
 * Returns: a string value, or %NULL if never set.
 *
 * Since: 1.1.2
 **/
const gchar *
fu_device_get_logical_id (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	return priv->logical_id;
}

/**
 * fu_device_set_logical_id:
 * @self: A #FuDevice
 * @logical_id: a string, e.g. `dev2`
 *
 * Sets the logical ID on the device. This is designed to disambiguate devices
 * with the same physical ID.
 *
 * Since: 1.1.2
 **/
void
fu_device_set_logical_id (FuDevice *self, const gchar *logical_id)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));

	/* not changed */
	if (g_strcmp0 (priv->logical_id, logical_id) == 0)
		return;

	g_free (priv->logical_id);
	priv->logical_id = g_strdup (logical_id);
	priv->device_id_valid = FALSE;
	g_object_notify (G_OBJECT (self), "logical-id");
}

/**
 * fu_device_get_backend_id:
 * @self: A #FuDevice
 *
 * Gets the ID set for the device as recognised by the backend. This is typically
 * a Linux sysfs path or USB platform ID. If unset, it also falls back to the
 * physical ID as this may be the same value.
 *
 * Returns: a string value, or %NULL if never set.
 *
 * Since: 1.5.8
 **/
const gchar *
fu_device_get_backend_id (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	if (priv->backend_id != NULL)
		return priv->backend_id;
	return priv->physical_id;
}

/**
 * fu_device_set_backend_id:
 * @self: A #FuDevice
 * @backend_id: a string, e.g. `dev2`
 *
 * Sets the backend ID on the device. This is designed to disambiguate devices
 * with the same physical ID. This is typically a Linux sysfs path or USB
 * platform ID.
 *
 * Since: 1.5.8
 **/
void
fu_device_set_backend_id (FuDevice *self, const gchar *backend_id)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));

	/* not changed */
	if (g_strcmp0 (priv->backend_id, backend_id) == 0)
		return;

	g_free (priv->backend_id);
	priv->backend_id = g_strdup (backend_id);
	priv->device_id_valid = FALSE;
	g_object_notify (G_OBJECT (self), "backend-id");
}

/**
 * fu_device_get_proxy_guid:
 * @self: A #FuDevice
 *
 * Gets the proxy GUID device, which which is set to let the engine match up the
 * proxy between plugins.
 *
 * Returns: a string value, or %NULL if never set.
 *
 * Since: 1.4.1
 **/
const gchar *
fu_device_get_proxy_guid (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	return priv->proxy_guid;
}

/**
 * fu_device_set_proxy_guid:
 * @self: A #FuDevice
 * @proxy_guid: a string, e.g. `USB\VID_413C&PID_B06E&hub`
 *
 * Sets the GUID of the proxy device. The proxy device may update @self.
 *
 * Since: 1.4.1
 **/
void
fu_device_set_proxy_guid (FuDevice *self, const gchar *proxy_guid)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));

	/* not changed */
	if (g_strcmp0 (priv->proxy_guid, proxy_guid) == 0)
		return;

	g_free (priv->proxy_guid);
	priv->proxy_guid = g_strdup (proxy_guid);
}

/**
 * fu_device_get_protocol:
 * @self: A #FuDevice
 *
 * Gets the protocol ID on the device.
 *
 * Returns: a string value e.g. `org.hughski.colorhug`, or %NULL
 *
 * Since: 1.3.5
 **/
const gchar *
fu_device_get_protocol (FuDevice *self)
{
	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	return fwupd_device_get_protocol (FWUPD_DEVICE (self));
}

/**
 * fu_device_set_protocol:
 * @self: A #FuDevice
 * @protocol: a defined protocol ID, e.g. `org.hughski.colorhug`
 *
 * Sets the protocol ID on the device.
 *
 * Since: 1.3.5
 **/
void
fu_device_set_protocol (FuDevice *self, const gchar *protocol)
{
	g_return_if_fail (FU_IS_DEVICE (self));
	fwupd_device_set_protocol (FWUPD_DEVICE (self), protocol);
}

/**
 * fu_device_set_physical_id:
 * @self: A #FuDevice
 * @physical_id: a string that identifies the physical device connection
 *
 * Sets the physical ID on the device which represents the electrical connection
 * of the device to the system. Multiple #FuDevices can share a physical ID.
 *
 * The physical ID is used to remove logical devices when a physical device has
 * been removed from the system.
 *
 * A sysfs or devpath is not a physical ID, but could be something like
 * `PCI_SLOT_NAME=0000:3e:00.0`.
 *
 * Since: 1.1.2
 **/
void
fu_device_set_physical_id (FuDevice *self, const gchar *physical_id)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));
	g_return_if_fail (physical_id != NULL);

	/* not changed */
	if (g_strcmp0 (priv->physical_id, physical_id) == 0)
		return;

	g_free (priv->physical_id);
	priv->physical_id = g_strdup (physical_id);
	priv->device_id_valid = FALSE;
	g_object_notify (G_OBJECT (self), "physical-id");
}

/**
 * fu_device_get_physical_id:
 * @self: A #FuDevice
 *
 * Gets the physical ID set for the device, which represents the electrical
 * connection used to compare devices.
 *
 * Multiple #FuDevices can share a single physical ID.
 *
 * Returns: a string value, or %NULL if never set.
 *
 * Since: 1.1.2
 **/
const gchar *
fu_device_get_physical_id (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	return priv->physical_id;
}

/**
 * fu_device_add_flag:
 * @self: A #FuDevice
 * @flag: A #FwupdDeviceFlags
 *
 * Adds a device flag to the device
 *
 * Since: 0.1.0
 **/
void
fu_device_add_flag (FuDevice *self, FwupdDeviceFlags flag)
{
	/* none is not used as an "exported" flag */
	if (flag == FWUPD_DEVICE_FLAG_NONE)
		return;

	/* being both a bootloader and requiring a bootloader is invalid */
	if (flag & FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER)
		fu_device_remove_flag (self, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	if (flag & FWUPD_DEVICE_FLAG_IS_BOOTLOADER)
		fu_device_remove_flag (self, FWUPD_DEVICE_FLAG_NEEDS_BOOTLOADER);

	/* one implies the other */
	if (flag & FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE)
		flag |= FWUPD_DEVICE_FLAG_CAN_VERIFY;
	if (flag & FWUPD_DEVICE_FLAG_INSTALL_ALL_RELEASES)
		flag |= FWUPD_DEVICE_FLAG_VERSION_CHECK_REQUIRED;
	fwupd_device_add_flag (FWUPD_DEVICE (self), flag);
}

static void
fu_device_set_custom_flag (FuDevice *self, const gchar *hint)
{
	FwupdDeviceFlags flag;
	FuDeviceInternalFlags internal_flag;

	/* is this a negated device flag */
	if (g_str_has_prefix (hint, "~")) {
		flag = fwupd_device_flag_from_string (hint + 1);
		if (flag != FWUPD_DEVICE_FLAG_UNKNOWN)
			fu_device_remove_flag (self, flag);
		internal_flag = fu_device_internal_flag_from_string (hint + 1);
		if (internal_flag != FU_DEVICE_INTERNAL_FLAG_UNKNOWN)
			fu_device_remove_internal_flag (self, internal_flag);
		return;
	}

	/* is this a known device flag */
	flag = fwupd_device_flag_from_string (hint);
	if (flag != FWUPD_DEVICE_FLAG_UNKNOWN)
		fu_device_add_flag (self, flag);
	internal_flag = fu_device_internal_flag_from_string (hint);
	if (internal_flag != FU_DEVICE_INTERNAL_FLAG_UNKNOWN)
		fu_device_remove_internal_flag (self, internal_flag);
}

/**
 * fu_device_set_custom_flags:
 * @self: A #FuDevice
 * @custom_flags: a string
 *
 * Sets the custom flags from the quirk system that can be used to
 * affect device matching. The actual string format is defined by the plugin.
 *
 * Since: 1.1.0
 **/
void
fu_device_set_custom_flags (FuDevice *self, const gchar *custom_flags)
{
	g_return_if_fail (FU_IS_DEVICE (self));
	g_return_if_fail (custom_flags != NULL);

	/* display what was set when converting to a string */
	fu_device_set_metadata (self, "CustomFlags", custom_flags);

	/* look for any standard FwupdDeviceFlags */
	if (custom_flags != NULL) {
		g_auto(GStrv) hints = g_strsplit (custom_flags, ",", -1);
		for (guint i = 0; hints[i] != NULL; i++)
			fu_device_set_custom_flag (self, hints[i]);
	}
}

/**
 * fu_device_get_custom_flags:
 * @self: A #FuDevice
 *
 * Gets the custom flags for the device from the quirk system.
 *
 * Returns: a string value, or %NULL if never set.
 *
 * Since: 1.1.0
 **/
const gchar *
fu_device_get_custom_flags (FuDevice *self)
{
	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	return fu_device_get_metadata (self, "CustomFlags");
}

/**
 * fu_device_has_custom_flag:
 * @self: A #FuDevice
 * @hint: A string, e.g. "bootloader"
 *
 * Checks if the custom flag exists for the device from the quirk system.
 *
 * It may be more efficient to call fu_device_get_custom_flags() and split the
 * string locally if checking for lots of different flags.
 *
 * Returns: %TRUE if the hint exists
 *
 * Since: 1.1.0
 **/
gboolean
fu_device_has_custom_flag (FuDevice *self, const gchar *hint)
{
	const gchar *hint_str;
	g_auto(GStrv) hints = NULL;

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (hint != NULL, FALSE);

	/* no hint is perfectly valid */
	hint_str = fu_device_get_custom_flags (self);
	if (hint_str == NULL)
		return FALSE;
	hints = g_strsplit (hint_str, ",", -1);
	return g_strv_contains ((const gchar * const *) hints, hint);
}

/**
 * fu_device_get_remove_delay:
 * @self: A #FuDevice
 *
 * Returns the maximum delay expected when replugging the device going into
 * bootloader mode.
 *
 * Returns: time in milliseconds
 *
 * Since: 1.0.2
 **/
guint
fu_device_get_remove_delay (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_DEVICE (self), 0);
	return priv->remove_delay;
}

/**
 * fu_device_set_remove_delay:
 * @self: A #FuDevice
 * @remove_delay: the remove_delay value
 *
 * Sets the amount of time a device is allowed to return in bootloader mode.
 *
 * NOTE: this should be less than 3000ms for devices that just have to reset
 * and automatically re-enumerate, but significantly longer if it involves a
 * user removing a cable, pressing several buttons and removing a cable.
 * A suggested value for this would be 10,000ms.
 *
 * Since: 1.0.2
 **/
void
fu_device_set_remove_delay (FuDevice *self, guint remove_delay)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));
	priv->remove_delay = remove_delay;
}

/**
 * fu_device_get_status:
 * @self: A #FuDevice
 *
 * Returns what the device is currently doing.
 *
 * Returns: the status value, e.g. %FWUPD_STATUS_DEVICE_WRITE
 *
 * Since: 1.0.3
 **/
FwupdStatus
fu_device_get_status (FuDevice *self)
{
	g_return_val_if_fail (FU_IS_DEVICE (self), 0);
	return fwupd_device_get_status (FWUPD_DEVICE (self));
}

/**
 * fu_device_set_status:
 * @self: A #FuDevice
 * @status: the status value, e.g. %FWUPD_STATUS_DEVICE_WRITE
 *
 * Sets what the device is currently doing.
 *
 * Since: 1.0.3
 **/
void
fu_device_set_status (FuDevice *self, FwupdStatus status)
{
	g_return_if_fail (FU_IS_DEVICE (self));
	fwupd_device_set_status (FWUPD_DEVICE (self), status);
}

/**
 * fu_device_get_progress:
 * @self: A #FuDevice
 *
 * Returns the progress completion.
 *
 * Returns: value in percent
 *
 * Since: 1.0.3
 **/
guint
fu_device_get_progress (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_DEVICE (self), 0);
	return priv->progress;
}

/**
 * fu_device_set_progress:
 * @self: A #FuDevice
 * @progress: the progress percentage value
 *
 * Sets the progress completion.
 *
 * Since: 1.0.3
 **/
void
fu_device_set_progress (FuDevice *self, guint progress)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));
	if (priv->progress == progress)
		return;
	priv->progress = progress;
	g_object_notify (G_OBJECT (self), "progress");
}

/**
 * fu_device_set_progress_full:
 * @self: A #FuDevice
 * @progress_done: the bytes already done
 * @progress_total: the total number of bytes
 *
 * Sets the progress completion using the raw progress values.
 *
 * Since: 1.0.3
 **/
void
fu_device_set_progress_full (FuDevice *self, gsize progress_done, gsize progress_total)
{
	gdouble percentage = 0.f;
	g_return_if_fail (FU_IS_DEVICE (self));
	if (progress_total > 0)
		percentage = (100.f * (gdouble) progress_done) / (gdouble) progress_total;
	fu_device_set_progress (self, (guint) percentage);
}

/**
 * fu_device_sleep_with_progress:
 * @self: A #FuDevice
 * @delay_secs: the delay in seconds
 *
 * Sleeps, setting the device progress from 0..100% as time continues.
 * The value is gven in whole seconds as it does not make sense to show the
 * progressbar advancing so quickly for durations of less than one second.
 *
 * Since: 1.5.0
 **/
void
fu_device_sleep_with_progress (FuDevice *self, guint delay_secs)
{
	gulong delay_us_pc = (delay_secs * G_USEC_PER_SEC) / 100;

	g_return_if_fail (FU_IS_DEVICE (self));
	g_return_if_fail (delay_secs > 0);

	fu_device_set_progress (self, 0);
	for (guint i = 0; i < 100; i++) {
		g_usleep (delay_us_pc);
		fu_device_set_progress (self, i + 1);
	}
}

static void
fu_device_add_string (FuDevice *self, guint idt, GString *str)
{
	GPtrArray *children;
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_autofree gchar *tmp = NULL;
	g_autoptr(GRWLockReaderLocker) locker = g_rw_lock_reader_locker_new (&priv->metadata_mutex);

	g_return_if_fail (locker != NULL);

	/* subclassed type */
	fu_common_string_append_kv (str, idt, G_OBJECT_TYPE_NAME (self), NULL);

	tmp = fwupd_device_to_string (FWUPD_DEVICE (self));
	if (tmp != NULL && tmp[0] != '\0')
		g_string_append (str, tmp);
	if (priv->alternate_id != NULL)
		fu_common_string_append_kv (str, idt + 1, "AlternateId", priv->alternate_id);
	if (priv->equivalent_id != NULL)
		fu_common_string_append_kv (str, idt + 1, "EquivalentId", priv->equivalent_id);
	if (priv->physical_id != NULL)
		fu_common_string_append_kv (str, idt + 1, "PhysicalId", priv->physical_id);
	if (priv->logical_id != NULL)
		fu_common_string_append_kv (str, idt + 1, "LogicalId", priv->logical_id);
	if (priv->backend_id != NULL)
		fu_common_string_append_kv (str, idt + 1, "BackendId", priv->backend_id);
	if (priv->proxy != NULL)
		fu_common_string_append_kv (str, idt + 1, "ProxyId", fu_device_get_id (priv->proxy));
	if (priv->proxy_guid != NULL)
		fu_common_string_append_kv (str, idt + 1, "ProxyGuid", priv->proxy_guid);
	if (priv->size_min > 0) {
		g_autofree gchar *sz = g_strdup_printf ("%" G_GUINT64_FORMAT, priv->size_min);
		fu_common_string_append_kv (str, idt + 1, "FirmwareSizeMin", sz);
	}
	if (priv->size_max > 0) {
		g_autofree gchar *sz = g_strdup_printf ("%" G_GUINT64_FORMAT, priv->size_max);
		fu_common_string_append_kv (str, idt + 1, "FirmwareSizeMax", sz);
	}
	if (priv->order != G_MAXINT)
		fu_common_string_append_ku (str, idt + 1, "Order", priv->order);
	if (priv->priority > 0)
		fu_common_string_append_ku (str, idt + 1, "Priority", priv->priority);
	if (priv->metadata != NULL) {
		g_autoptr(GList) keys = g_hash_table_get_keys (priv->metadata);
		for (GList *l = keys; l != NULL; l = l->next) {
			const gchar *key = l->data;
			const gchar *value = g_hash_table_lookup (priv->metadata, key);
			fu_common_string_append_kv (str, idt + 1, key, value);
		}
	}
	for (guint i = 0; i < priv->possible_plugins->len; i++) {
		const gchar *name = g_ptr_array_index (priv->possible_plugins, i);
		fu_common_string_append_kv (str, idt + 1, "PossiblePlugin", name);
	}
	if (priv->internal_flags != FU_DEVICE_INTERNAL_FLAG_NONE) {
		g_autoptr(GString) tmp2 = g_string_new ("");
		for (guint i = 0; i < 64; i++) {
			if ((priv->internal_flags & ((guint64) 1 << i)) == 0)
				continue;
			g_string_append_printf (tmp2, "%s|",
						fu_device_internal_flag_to_string ((guint64) 1 << i));
		}
		if (tmp2->len > 0)
			g_string_truncate (tmp2, tmp2->len - 1);
		fu_common_string_append_kv (str, idt + 1, "PrivateFlags", tmp2->str);
	}

	/* subclassed */
	if (klass->to_string != NULL)
		klass->to_string (self, idt + 1, str);

	/* print children also */
	children = fu_device_get_children (self);
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index (children, i);
		fu_device_add_string (child, idt + 1, str);
	}
}

/**
 * fu_device_to_string:
 * @self: A #FuDevice
 *
 * This allows us to easily print the FwupdDevice, the FwupdRelease and the
 * daemon-specific metadata.
 *
 * Returns: a string value, or %NULL for invalid.
 *
 * Since: 0.9.8
 **/
gchar *
fu_device_to_string (FuDevice *self)
{
	GString *str = g_string_new (NULL);
	fu_device_add_string (self, 0, str);
	return g_string_free (str, FALSE);
}

/**
 * fu_device_set_quirks:
 * @self: A #FuDevice
 * @quirks: A #FuQuirks, or %NULL
 *
 * Sets the optional quirk information which may be useful to this device.
 * This is typically set after the #FuDevice has been created, but before
 * the device has been opened or probed.
 *
 * Since: 1.0.3
 **/
void
fu_device_set_quirks (FuDevice *self, FuQuirks *quirks)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));
	if (g_set_object (&priv->quirks, quirks))
		g_object_notify (G_OBJECT (self), "quirks");
}

/**
 * fu_device_get_quirks:
 * @self: A #FuDevice
 *
 * Gets the quirk information which may be useful to this device.
 *
 * Returns: (transfer none): the #FuQuirks object, or %NULL
 *
 * Since: 1.0.3
 **/
FuQuirks *
fu_device_get_quirks (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	return priv->quirks;
}

/**
 * fu_device_get_release_default:
 * @self: A #FuDevice
 *
 * Gets the default release for the device, creating one if not found.
 *
 * Returns: (transfer none): the #FwupdRelease object
 *
 * Since: 1.0.5
 **/
FwupdRelease *
fu_device_get_release_default (FuDevice *self)
{
	g_autoptr(FwupdRelease) rel = NULL;
	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	if (fwupd_device_get_release_default (FWUPD_DEVICE (self)) != NULL)
		return fwupd_device_get_release_default (FWUPD_DEVICE (self));
	rel = fwupd_release_new ();
	fwupd_device_add_release (FWUPD_DEVICE (self), rel);
	return rel;
}

/**
 * fu_device_write_firmware:
 * @self: A #FuDevice
 * @fw: A #GBytes
 * @flags: #FwupdInstallFlags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: A #GError
 *
 * Writes firmware to the device by calling a plugin-specific vfunc.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0.8
 **/
gboolean
fu_device_write_firmware (FuDevice *self,
			  GBytes *fw,
			  FwupdInstallFlags flags,
			  GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);
	g_autoptr(FuFirmware) firmware = NULL;
	g_autofree gchar *str = NULL;

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no plugin-specific method */
	if (klass->write_firmware == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported");
		return FALSE;
	}

	/* prepare (e.g. decompress) firmware */
	firmware = fu_device_prepare_firmware (self, fw, flags, error);
	if (firmware == NULL)
		return FALSE;
	str = fu_firmware_to_string (firmware);
	g_debug ("installing onto %s:\n%s", fu_device_get_id (self), str);

	/* call vfunc */
	return klass->write_firmware (self, firmware, flags, error);
}

/**
 * fu_device_prepare_firmware:
 * @self: A #FuDevice
 * @fw: A #GBytes
 * @flags: #FwupdInstallFlags, e.g. %FWUPD_INSTALL_FLAG_FORCE
 * @error: A #GError
 *
 * Prepares the firmware by calling an optional device-specific vfunc for the
 * device, which can do things like decompressing or parsing of the firmware
 * data.
 *
 * For all firmware, this checks the size of the firmware if limits have been
 * set using fu_device_set_firmware_size_min(), fu_device_set_firmware_size_max()
 * or using a quirk entry.
 *
 * Returns: (transfer full): A new #GBytes, or %NULL for error
 *
 * Since: 1.1.2
 **/
FuFirmware *
fu_device_prepare_firmware (FuDevice *self,
			    GBytes *fw,
			    FwupdInstallFlags flags,
			    GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) fw_def = NULL;

	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	g_return_val_if_fail (fw != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* optionally subclassed */
	if (klass->prepare_firmware != NULL) {
		fu_device_set_status (self, FWUPD_STATUS_DECOMPRESSING);
		firmware = klass->prepare_firmware (self, fw, flags, error);
		if (firmware == NULL)
			return NULL;
	} else {
		firmware = fu_firmware_new_from_bytes (fw);
	}

	/* check size */
	fw_def = fu_firmware_get_image_default_bytes (firmware, NULL);
	if (fw_def != NULL) {
		guint64 fw_sz = (guint64) g_bytes_get_size (fw_def);
		if (priv->size_max > 0 && fw_sz > priv->size_max) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "firmware is %04x bytes larger than the allowed "
				     "maximum size of %04x bytes",
				     (guint) (fw_sz - priv->size_max),
				     (guint) priv->size_max);
			return NULL;
		}
		if (priv->size_min > 0 && fw_sz < priv->size_min) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INVALID_FILE,
				     "firmware is %04x bytes smaller than the allowed "
				     "minimum size of %04x bytes",
				     (guint) (priv->size_min - fw_sz),
				     (guint) priv->size_max);
			return NULL;
		}
	}

	/* success */
	return g_steal_pointer (&firmware);
}

/**
 * fu_device_read_firmware:
 * @self: A #FuDevice
 * @error: A #GError
 *
 * Reads firmware from the device by calling a plugin-specific vfunc.
 * The device subclass should try to ensure the firmware does not contain any
 * serial numbers or user-configuration values and can be used to calculate the
 * device checksum.
 *
 * The return value can be converted to a blob of memory using fu_firmware_write().
 *
 * Returns: (transfer full): A #FuFirmware, or %NULL for error
 *
 * Since: 1.0.8
 **/
FuFirmware *
fu_device_read_firmware (FuDevice *self, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);
	g_autoptr(GBytes) fw = NULL;

	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* device does not support reading for verification CRCs */
	if (!fu_device_has_flag (self, FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE)) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported");
		return NULL;
	}

	/* call vfunc */
	if (klass->read_firmware != NULL)
		return klass->read_firmware (self, error);

	/* use the default FuFirmware when only ->dump_firmware is provided */
	fw = fu_device_dump_firmware (self, error);
	if (fw == NULL)
		return NULL;
	return fu_firmware_new_from_bytes (fw);
}

/**
 * fu_device_dump_firmware:
 * @self: A #FuDevice
 * @error: A #GError
 *
 * Reads the raw firmware image from the device by calling a plugin-specific
 * vfunc. This raw firmware image may contain serial numbers or device-specific
 * configuration but should be a byte-for-byte match compared to using an
 * external SPI programmer.
 *
 * Returns: (transfer full): A #GBytes, or %NULL for error
 *
 * Since: 1.5.0
 **/
GBytes *
fu_device_dump_firmware (FuDevice *self, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);

	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* use the default FuFirmware when only ->dump_firmware is provided */
	if (klass->dump_firmware == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported");
		return NULL;
	}

	/* proxy */
	return klass->dump_firmware (self, error);
}

/**
 * fu_device_detach:
 * @self: A #FuDevice
 * @error: A #GError
 *
 * Detaches a device from the application into bootloader mode.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0.8
 **/
gboolean
fu_device_detach (FuDevice *self, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no plugin-specific method */
	if (klass->detach == NULL)
		return TRUE;

	/* call vfunc */
	return klass->detach (self, error);
}

/**
 * fu_device_attach:
 * @self: A #FuDevice
 * @error: A #GError
 *
 * Attaches a device from the bootloader into application mode.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.0.8
 **/
gboolean
fu_device_attach (FuDevice *self, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no plugin-specific method */
	if (klass->attach == NULL)
		return TRUE;

	/* call vfunc */
	return klass->attach (self, error);
}

/**
 * fu_device_reload:
 * @self: A #FuDevice
 * @error: A #GError
 *
 * Reloads a device that has just gone from bootloader into application mode.
 *
 * Returns: %TRUE on success
 *
 * Since: 1.3.3
 **/
gboolean
fu_device_reload (FuDevice *self, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no plugin-specific method */
	if (klass->reload == NULL)
		return TRUE;

	/* call vfunc */
	return klass->reload (self, error);
}

/**
 * fu_device_prepare:
 * @self: A #FuDevice
 * @flags: A #FwupdInstallFlags
 * @error: A #GError
 *
 * Prepares a device for update. A different plugin can handle each of
 * FuDevice->prepare(), FuDevice->detach() and FuDevice->write_firmware().
 *
 * Returns: %TRUE on success
 *
 * Since: 1.3.3
 **/
gboolean
fu_device_prepare (FuDevice *self, FwupdInstallFlags flags, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no plugin-specific method */
	if (klass->prepare == NULL)
		return TRUE;

	/* call vfunc */
	return klass->prepare (self, flags, error);
}

/**
 * fu_device_cleanup:
 * @self: A #FuDevice
 * @flags: A #FwupdInstallFlags
 * @error: A #GError
 *
 * Cleans up a device after an update. A different plugin can handle each of
 * FuDevice->write_firmware(), FuDevice->attach() and FuDevice->cleanup().
 *
 * Returns: %TRUE on success
 *
 * Since: 1.3.3
 **/
gboolean
fu_device_cleanup (FuDevice *self, FwupdInstallFlags flags, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* no plugin-specific method */
	if (klass->cleanup == NULL)
		return TRUE;

	/* call vfunc */
	return klass->cleanup (self, flags, error);
}

static gboolean
fu_device_open_cb (FuDevice *self, gpointer user_data, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);
	return klass->open (self, error);
}

/**
 * fu_device_open:
 * @self: A #FuDevice
 * @error: A #GError, or %NULL
 *
 * Opens a device, optionally running a object-specific vfunc.
 *
 * Plugins can call fu_device_open() multiple times without calling
 * fu_device_close(), but only the first call will actually invoke the vfunc.
 *
 * It is expected that plugins issue the same number of fu_device_open() and
 * fu_device_close() methods when using a specific @self.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.1.2
 **/
gboolean
fu_device_open (FuDevice *self, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);
	FuDevicePrivate *priv = GET_PRIVATE (self);

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already open */
	g_atomic_int_inc (&priv->open_refcount);
	if (priv->open_refcount > 1)
		return TRUE;

	/* probe */
	if (!fu_device_probe (self, error))
		return FALSE;

	/* ensure the device ID is already setup */
	if (!fu_device_ensure_id (self, error))
		return FALSE;

	/* subclassed */
	if (klass->open != NULL) {
		if (fu_device_has_internal_flag (self, FU_DEVICE_INTERNAL_FLAG_RETRY_OPEN)) {
			if (!fu_device_retry_full (self, fu_device_open_cb,
						   FU_DEVICE_RETRY_OPEN_COUNT,
						   FU_DEVICE_RETRY_OPEN_DELAY,
						   NULL, error))
				return FALSE;
		} else {
			if (!klass->open (self, error))
				return FALSE;
		}
	}

	/* setup */
	if (!fu_device_setup (self, error))
		return FALSE;

	/* ensure the device ID is still valid */
	if (!fu_device_ensure_id (self, error))
		return FALSE;

	/* success */
	return TRUE;
}

/**
 * fu_device_close:
 * @self: A #FuDevice
 * @error: A #GError, or %NULL
 *
 * Closes a device, optionally running a object-specific vfunc.
 *
 * Plugins can call fu_device_close() multiple times without calling
 * fu_device_open(), but only the last call will actually invoke the vfunc.
 *
 * It is expected that plugins issue the same number of fu_device_open() and
 * fu_device_close() methods when using a specific @self.
 *
 * An error is returned if this method is called without having used the
 * fu_device_open() method beforehand.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.1.2
 **/
gboolean
fu_device_close (FuDevice *self, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);
	FuDevicePrivate *priv = GET_PRIVATE (self);

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not yet open */
	if (priv->open_refcount == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "cannot close device, refcount already zero");
		return FALSE;
	}
	if (!g_atomic_int_dec_and_test (&priv->open_refcount))
		return TRUE;

	/* subclassed */
	if (klass->close != NULL) {
		if (!klass->close (self, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_device_probe:
 * @self: A #FuDevice
 * @error: A #GError, or %NULL
 *
 * Probes a device, setting parameters on the object that does not need
 * the device open or the interface claimed.
 * If the device is not compatible then an error should be returned.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.1.2
 **/
gboolean
fu_device_probe (FuDevice *self, GError **error)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already done */
	if (priv->done_probe)
		return TRUE;

	/* subclassed */
	if (klass->probe != NULL) {
		if (!klass->probe (self, error))
			return FALSE;
	}
	priv->done_probe = TRUE;
	return TRUE;
}

/**
 * fu_device_rescan:
 * @self: A #FuDevice
 * @error: A #GError, or %NULL
 *
 * Rescans a device, re-adding GUIDs or flags based on some hardware change.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.3.1
 **/
gboolean
fu_device_rescan (FuDevice *self, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* remove all GUIDs */
	g_ptr_array_set_size (fu_device_get_instance_ids (self), 0);
	g_ptr_array_set_size (fu_device_get_guids (self), 0);

	/* subclassed */
	if (klass->rescan != NULL) {
		if (!klass->rescan (self, error)) {
			fu_device_convert_instance_ids (self);
			return FALSE;
		}
	}

	fu_device_convert_instance_ids (self);
	return TRUE;
}

/**
 * fu_device_convert_instance_ids:
 * @self: A #FuDevice
 *
 * Converts all the Device Instance IDs added using fu_device_add_instance_id()
 * into actual GUIDs, **unless** %FU_DEVICE_INTERNAL_FLAG_NO_AUTO_INSTANCE_IDS has
 * been set.
 *
 * Plugins will only need to need to call this manually when adding child
 * devices, as fu_device_setup() automatically calls this after the
 * fu_device_probe() and fu_device_setup() virtual functions have been run.
 *
 * Since: 1.2.5
 **/
void
fu_device_convert_instance_ids (FuDevice *self)
{
	GPtrArray *instance_ids;

	/* OEM specific hardware */
	if (fu_device_has_internal_flag (self, FU_DEVICE_INTERNAL_FLAG_NO_AUTO_INSTANCE_IDS))
		return;
	instance_ids = fwupd_device_get_instance_ids (FWUPD_DEVICE (self));
	for (guint i = 0; i < instance_ids->len; i++) {
		const gchar *instance_id = g_ptr_array_index (instance_ids, i);
		g_autofree gchar *guid = fwupd_guid_hash_string (instance_id);
		fwupd_device_add_guid (FWUPD_DEVICE (self), guid);
	}
}

/**
 * fu_device_setup:
 * @self: A #FuDevice
 * @error: A #GError, or %NULL
 *
 * Sets up a device, setting parameters on the object that requires
 * the device to be open and have the interface claimed.
 * If the device is not compatible then an error should be returned.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.1.2
 **/
gboolean
fu_device_setup (FuDevice *self, GError **error)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);
	GPtrArray *children;

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* already done */
	if (priv->done_setup)
		return TRUE;

	/* subclassed */
	if (klass->setup != NULL) {
		if (!klass->setup (self, error))
			return FALSE;
	}

	/* run setup on the children too (unless done already) */
	children = fu_device_get_children (self);
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child_tmp = g_ptr_array_index (children, i);
		if (!fu_device_setup (child_tmp, error))
			return FALSE;
	}

	/* convert the instance IDs to GUIDs */
	fu_device_convert_instance_ids (self);

	priv->done_setup = TRUE;
	return TRUE;
}

/**
 * fu_device_activate:
 * @self: A #FuDevice
 * @error: A #GError, or %NULL
 *
 * Activates up a device, which normally means the device switches to a new
 * firmware version. This should only be called when data loss cannot occur.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.2.6
 **/
gboolean
fu_device_activate (FuDevice *self, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* subclassed */
	if (klass->activate != NULL) {
		if (!klass->activate (self, error))
			return FALSE;
	}

	return TRUE;
}

/**
 * fu_device_probe_invalidate:
 * @self: A #FuDevice
 *
 * Normally when calling fu_device_probe() multiple times it is only done once.
 * Calling this method causes the next requests to fu_device_probe() and
 * fu_device_setup() actually probe the hardware.
 *
 * This should be done in case the backing device has changed, for instance if
 * a USB device has been replugged.
 *
 * Since: 1.1.2
 **/
void
fu_device_probe_invalidate (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_DEVICE (self));
	priv->done_probe = FALSE;
	priv->done_setup = FALSE;
}

/**
 * fu_device_report_metadata_pre:
 * @self: A #FuDevice
 *
 * Collects metadata that would be useful for debugging a failed update report.
 *
 * Returns: (transfer full) (nullable): A #GHashTable, or %NULL if there is no data
 *
 * Since: 1.5.0
 **/
GHashTable *
fu_device_report_metadata_pre (FuDevice *self)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);
	g_autoptr(GHashTable) metadata = NULL;

	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);

	/* not implemented */
	if (klass->report_metadata_pre == NULL)
		return NULL;

	/* metadata for all devices */
	metadata = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	klass->report_metadata_pre (self, metadata);
	return g_steal_pointer (&metadata);
}

/**
 * fu_device_report_metadata_post:
 * @self: A #FuDevice
 *
 * Collects metadata that would be useful for debugging a failed update report.
 *
 * Returns: (transfer full) (nullable): A #GHashTable, or %NULL if there is no data
 *
 * Since: 1.5.0
 **/
GHashTable *
fu_device_report_metadata_post (FuDevice *self)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);
	g_autoptr(GHashTable) metadata = NULL;

	g_return_val_if_fail (FU_IS_DEVICE (self), NULL);

	/* not implemented */
	if (klass->report_metadata_post == NULL)
		return NULL;

	/* metadata for all devices */
	metadata = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	klass->report_metadata_post (self, metadata);
	return g_steal_pointer (&metadata);
}

/**
 * fu_device_bind_driver:
 * @self: A #FuDevice
 * @subsystem: A subsystem string, e.g. `pci`
 * @driver: A kernel module name, e.g. `tg3`
 * @error: A #GError, or %NULL
 *
 * Binds a driver to the device, which normally means the kernel driver takes
 * control of the hardware.
 *
 * Returns: %TRUE if driver was bound.
 *
 * Since: 1.5.0
 **/
gboolean
fu_device_bind_driver (FuDevice *self,
		       const gchar *subsystem,
		       const gchar *driver,
		       GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (subsystem != NULL, FALSE);
	g_return_val_if_fail (driver != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not implemented */
	if (klass->bind_driver == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported");
		return FALSE;
	}

	/* subclass */
	return klass->bind_driver (self, subsystem, driver, error);
}

/**
 * fu_device_unbind_driver:
 * @self: A #FuDevice
 * @error: A #GError, or %NULL
 *
 * Unbinds the driver from the device, which normally means the kernel releases
 * the hardware so it can be used from userspace.
 *
 * If there is no driver bound then this function will return with success
 * without actually doing anything.
 *
 * Returns: %TRUE if driver was unbound.
 *
 * Since: 1.5.0
 **/
gboolean
fu_device_unbind_driver (FuDevice *self, GError **error)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);

	g_return_val_if_fail (FU_IS_DEVICE (self), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not implemented */
	if (klass->unbind_driver == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "not supported");
		return FALSE;
	}

	/* subclass */
	return klass->unbind_driver (self, error);
}

/**
 * fu_device_incorporate:
 * @self: A #FuDevice
 * @donor: Another #FuDevice
 *
 * Copy all properties from the donor object if they have not already been set.
 *
 * Since: 1.1.0
 **/
void
fu_device_incorporate (FuDevice *self, FuDevice *donor)
{
	FuDeviceClass *klass = FU_DEVICE_GET_CLASS (self);
	FuDevicePrivate *priv = GET_PRIVATE (self);
	FuDevicePrivate *priv_donor = GET_PRIVATE (donor);
	GPtrArray *instance_ids = fu_device_get_instance_ids (donor);
	GPtrArray *parent_guids = fu_device_get_parent_guids (donor);

	g_return_if_fail (FU_IS_DEVICE (self));
	g_return_if_fail (FU_IS_DEVICE (donor));

	/* copy from donor FuDevice if has not already been set */
	if (priv->alternate_id == NULL)
		fu_device_set_alternate_id (self, fu_device_get_alternate_id (donor));
	if (priv->equivalent_id == NULL)
		fu_device_set_equivalent_id (self, fu_device_get_equivalent_id (donor));
	if (priv->physical_id == NULL && priv_donor->physical_id != NULL)
		fu_device_set_physical_id (self, priv_donor->physical_id);
	if (priv->logical_id == NULL && priv_donor->logical_id != NULL)
		fu_device_set_logical_id (self, priv_donor->logical_id);
	if (priv->backend_id == NULL && priv_donor->backend_id != NULL)
		fu_device_set_backend_id (self, priv_donor->backend_id);
	if (priv->proxy == NULL && priv_donor->proxy != NULL)
		fu_device_set_proxy (self, priv_donor->proxy);
	if (priv->proxy_guid == NULL && priv_donor->proxy_guid != NULL)
		fu_device_set_proxy_guid (self, priv_donor->proxy_guid);
	if (priv->quirks == NULL)
		fu_device_set_quirks (self, fu_device_get_quirks (donor));
	g_rw_lock_reader_lock (&priv_donor->parent_guids_mutex);
	for (guint i = 0; i < parent_guids->len; i++)
		fu_device_add_parent_guid (self, g_ptr_array_index (parent_guids, i));
	g_rw_lock_reader_unlock (&priv_donor->parent_guids_mutex);
	g_rw_lock_reader_lock (&priv_donor->metadata_mutex);
	if (priv->metadata != NULL) {
		g_autoptr(GList) keys = g_hash_table_get_keys (priv_donor->metadata);
		for (GList *l = keys; l != NULL; l = l->next) {
			const gchar *key = l->data;
			if (g_hash_table_lookup (priv->metadata, key) == NULL) {
				const gchar *value = g_hash_table_lookup (priv_donor->metadata, key);
				fu_device_set_metadata (self, key, value);
			}
		}
	}
	g_rw_lock_reader_unlock (&priv_donor->metadata_mutex);

	/* now the base class, where all the interesting bits are */
	fwupd_device_incorporate (FWUPD_DEVICE (self), FWUPD_DEVICE (donor));

	/* set by the superclass */
	if (fu_device_get_id (self) != NULL)
		priv->device_id_valid = TRUE;

	/* optional subclass */
	if (klass->incorporate != NULL)
		klass->incorporate (self, donor);

	/* call the set_quirk_kv() vfunc for the superclassed object */
	for (guint i = 0; i < instance_ids->len; i++) {
		const gchar *instance_id = g_ptr_array_index (instance_ids, i);
		g_autofree gchar *guid = fwupd_guid_hash_string (instance_id);
		fu_device_add_guid_quirks (self, guid);
	}
}

/**
 * fu_device_incorporate_flag:
 * @self: A #FuDevice
 * @donor: Another #FuDevice
 * @flag: A #FwupdDeviceFlags value
 *
 * Copy the value of a specific flag from the donor object.
 *
 * Since: 1.3.5
 **/
void
fu_device_incorporate_flag (FuDevice *self, FuDevice *donor, FwupdDeviceFlags flag)
{
	if (fu_device_has_flag (donor, flag) && !fu_device_has_flag (self, flag)) {
		g_debug ("donor set %s", fwupd_device_flag_to_string (flag));
		fu_device_add_flag (self, flag);
	} else if (!fu_device_has_flag (donor, flag) && fu_device_has_flag (self, flag)) {
		g_debug ("donor unset %s", fwupd_device_flag_to_string (flag));
		fu_device_remove_flag (self, flag);
	}
}

/**
 * fu_device_incorporate_from_component: (skip):
 * @device: A #FuDevice
 * @component: A #XbNode
 *
 * Copy all properties from the donor AppStream component.
 *
 * Since: 1.2.4
 **/
void
fu_device_incorporate_from_component (FuDevice *self, XbNode *component)
{
	const gchar *tmp;
	g_return_if_fail (FU_IS_DEVICE (self));
	g_return_if_fail (XB_IS_NODE (component));
	tmp = xb_node_query_text (component, "custom/value[@key='LVFS::UpdateMessage']", NULL);
	if (tmp != NULL)
		fwupd_device_set_update_message (FWUPD_DEVICE (self), tmp);
	tmp = xb_node_query_text (component, "custom/value[@key='LVFS::UpdateImage']", NULL);
	if (tmp != NULL)
		fwupd_device_set_update_image (FWUPD_DEVICE (self), tmp);
}

static void
fu_device_class_init (FuDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GParamSpec *pspec;
	object_class->finalize = fu_device_finalize;
	object_class->get_property = fu_device_get_property;
	object_class->set_property = fu_device_set_property;

	pspec = g_param_spec_string ("physical-id", NULL, NULL, NULL,
				     G_PARAM_READWRITE |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_PHYSICAL_ID, pspec);

	pspec = g_param_spec_string ("logical-id", NULL, NULL, NULL,
				     G_PARAM_READWRITE |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_LOGICAL_ID, pspec);

	pspec = g_param_spec_string ("backend-id", NULL, NULL, NULL,
				     G_PARAM_READWRITE |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_BACKEND_ID, pspec);

	pspec = g_param_spec_uint ("progress", NULL, NULL,
				   0, 100, 0,
				   G_PARAM_READWRITE |
				   G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_PROGRESS, pspec);

	pspec = g_param_spec_object ("quirks", NULL, NULL,
				     FU_TYPE_QUIRKS,
				     G_PARAM_READWRITE |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_QUIRKS, pspec);

	pspec = g_param_spec_object ("proxy", NULL, NULL,
				     FU_TYPE_DEVICE,
				     G_PARAM_READWRITE |
				     G_PARAM_CONSTRUCT |
				     G_PARAM_STATIC_NAME);
	g_object_class_install_property (object_class, PROP_PROXY, pspec);
}

static void
fu_device_init (FuDevice *self)
{
	FuDevicePrivate *priv = GET_PRIVATE (self);
	priv->order = G_MAXINT;
	priv->parent_guids = g_ptr_array_new_with_free_func (g_free);
	priv->possible_plugins = g_ptr_array_new_with_free_func (g_free);
	priv->retry_recs = g_ptr_array_new_with_free_func (g_free);
	g_rw_lock_init (&priv->parent_guids_mutex);
	g_rw_lock_init (&priv->metadata_mutex);
}

static void
fu_device_finalize (GObject *object)
{
	FuDevice *self = FU_DEVICE (object);
	FuDevicePrivate *priv = GET_PRIVATE (self);

	g_rw_lock_clear (&priv->metadata_mutex);
	g_rw_lock_clear (&priv->parent_guids_mutex);

	if (priv->alternate != NULL)
		g_object_unref (priv->alternate);
	if (priv->proxy != NULL)
		g_object_remove_weak_pointer (G_OBJECT (priv->proxy), (gpointer *) &priv->proxy);
	if (priv->quirks != NULL)
		g_object_unref (priv->quirks);
	if (priv->poll_id != 0)
		g_source_remove (priv->poll_id);
	if (priv->metadata != NULL)
		g_hash_table_unref (priv->metadata);
	g_ptr_array_unref (priv->parent_guids);
	g_ptr_array_unref (priv->possible_plugins);
	g_ptr_array_unref (priv->retry_recs);
	g_free (priv->alternate_id);
	g_free (priv->equivalent_id);
	g_free (priv->physical_id);
	g_free (priv->logical_id);
	g_free (priv->backend_id);
	g_free (priv->proxy_guid);

	G_OBJECT_CLASS (fu_device_parent_class)->finalize (object);
}

/**
 * fu_device_new:
 *
 * Creates a new #Fudevice
 *
 * Since: 0.1.0
 **/
FuDevice *
fu_device_new (void)
{
	FuDevice *self = g_object_new (FU_TYPE_DEVICE, NULL);
	return FU_DEVICE (self);
}

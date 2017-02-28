/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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
#include <gmodule.h>
#include <appstream-glib.h>
#include <errno.h>
#include <string.h>
#include <gio/gunixinputstream.h>
#ifdef HAVE_VALGRIND
#include <valgrind.h>
#endif /* HAVE_VALGRIND */

#include "fu-plugin-private.h"
#include "fu-pending.h"

#define	FU_PLUGIN_COLDPLUG_DELAY_MAXIMUM	3000u	/* ms */

static void fu_plugin_finalize			 (GObject *object);

typedef struct {
	GModule			*module;
	GUsbContext		*usb_ctx;
	gboolean		 enabled;
	gchar			*name;
	GHashTable		*devices;	/* platform_id:GObject */
	GHashTable		*devices_delay;	/* FuDevice:FuPluginHelper */
	FuPluginData		*data;
} FuPluginPrivate;

enum {
	SIGNAL_DEVICE_ADDED,
	SIGNAL_DEVICE_REMOVED,
	SIGNAL_STATUS_CHANGED,
	SIGNAL_PERCENTAGE_CHANGED,
	SIGNAL_RECOLDPLUG,
	SIGNAL_SET_COLDPLUG_DELAY,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (FuPlugin, fu_plugin, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_plugin_get_instance_private (o))

typedef const gchar	*(*FuPluginGetNameFunc)		(void);
typedef void		 (*FuPluginInitFunc)		(FuPlugin	*plugin);
typedef gboolean	 (*FuPluginStartupFunc)		(FuPlugin	*plugin,
							 GError		**error);
typedef gboolean	 (*FuPluginDeviceFunc)		(FuPlugin	*plugin,
							 FuDevice	*device,
							 GError		**error);
typedef gboolean	 (*FuPluginVerifyFunc)		(FuPlugin	*plugin,
							 FuDevice	*device,
							 FuPluginVerifyFlags flags,
							 GError		**error);
typedef gboolean	 (*FuPluginUpdateFunc)		(FuPlugin	*plugin,
							 FuDevice	*device,
							 GBytes		*blob_fw,
							 FwupdInstallFlags flags,
							 GError		**error);

/**
 * fu_plugin_get_name:
 * @plugin: A #FuPlugin
 *
 * Gets the plugin name.
 *
 * Returns: a plugin name, or %NULL for unknown.
 *
 * Since: 0.8.0
 **/
const gchar *
fu_plugin_get_name (FuPlugin *plugin)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_val_if_fail (FU_IS_PLUGIN (plugin), NULL);
	return priv->name;
}

/**
 * fu_plugin_cache_lookup:
 * @plugin: A #FuPlugin
 * @id: the key
 *
 * Finds an object in the per-plugin cache.
 *
 * Returns: (transfer none): a #GObject, or %NULL for unfound.
 *
 * Since: 0.8.0
 **/
gpointer
fu_plugin_cache_lookup (FuPlugin *plugin, const gchar *id)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_val_if_fail (FU_IS_PLUGIN (plugin), NULL);
	g_return_val_if_fail (id != NULL, NULL);
	return g_hash_table_lookup (priv->devices, id);
}

/**
 * fu_plugin_cache_add:
 * @plugin: A #FuPlugin
 * @id: the key
 * @dev: a #GObject, typically a #FuDevice
 *
 * Adds an object to the per-plugin cache.
 *
 * Since: 0.8.0
 **/
void
fu_plugin_cache_add (FuPlugin *plugin, const gchar *id, gpointer dev)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_if_fail (FU_IS_PLUGIN (plugin));
	g_return_if_fail (id != NULL);
	g_hash_table_insert (priv->devices, g_strdup (id), g_object_ref (dev));
}

/**
 * fu_plugin_cache_remove:
 * @plugin: A #FuPlugin
 * @id: the key
 *
 * Removes an object from the per-plugin cache.
 *
 * Since: 0.8.0
 **/
void
fu_plugin_cache_remove (FuPlugin *plugin, const gchar *id)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_if_fail (FU_IS_PLUGIN (plugin));
	g_return_if_fail (id != NULL);
	g_hash_table_remove (priv->devices, id);
}

/**
 * fu_plugin_get_data:
 * @plugin: A #FuPlugin
 *
 * Gets the per-plugin allocated private data.
 *
 * Returns: (transfer full): a pointer to a structure, or %NULL for unset.
 *
 * Since: 0.8.0
 **/
FuPluginData *
fu_plugin_get_data (FuPlugin *plugin)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_val_if_fail (FU_IS_PLUGIN (plugin), NULL);
	return priv->data;
}

/**
 * fu_plugin_alloc_data:
 * @plugin: A #FuPlugin
 * @data_sz: the size to allocate
 *
 * Allocates the per-plugin allocated private data.
 *
 * Returns: (transfer full): a pointer to a structure, or %NULL for unset.
 *
 * Since: 0.8.0
 **/
FuPluginData *
fu_plugin_alloc_data (FuPlugin *plugin, gsize data_sz)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_val_if_fail (FU_IS_PLUGIN (plugin), NULL);
	if (priv->data != NULL) {
		g_critical ("fu_plugin_alloc_data() already used by plugin");
		return priv->data;
	}
	priv->data = g_malloc0 (data_sz);
	return priv->data;
}

/**
 * fu_plugin_get_usb_context:
 * @plugin: A #FuPlugin
 *
 * Gets the shared USB context that all plugins can use.
 *
 * Returns: (transfer none): a #GUsbContext.
 *
 * Since: 0.8.0
 **/
GUsbContext *
fu_plugin_get_usb_context (FuPlugin *plugin)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_val_if_fail (FU_IS_PLUGIN (plugin), NULL);
	return priv->usb_ctx;
}

void
fu_plugin_set_usb_context (FuPlugin *plugin, GUsbContext *usb_ctx)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	g_set_object (&priv->usb_ctx, usb_ctx);
}

/**
 * fu_plugin_get_enabled:
 * @plugin: A #FuPlugin
 *
 * Returns if the plugin is enabled.
 *
 * Returns: %TRUE if the plugin is currently enabled.
 *
 * Since: 0.8.0
 **/
gboolean
fu_plugin_get_enabled (FuPlugin *plugin)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_val_if_fail (FU_IS_PLUGIN (plugin), FALSE);
	return priv->enabled;
}

/**
 * fu_plugin_set_enabled:
 * @plugin: A #FuPlugin
 * @enabled: the enabled value
 *
 * Enables or disables a plugin. Plugins can self-disable at any point.
 *
 * Since: 0.8.0
 **/
void
fu_plugin_set_enabled (FuPlugin *plugin, gboolean enabled)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_if_fail (FU_IS_PLUGIN (plugin));
	priv->enabled = enabled;
}

gboolean
fu_plugin_open (FuPlugin *plugin, const gchar *filename, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginInitFunc func = NULL;
	gchar *str;

	priv->module = g_module_open (filename, 0);
	if (priv->module == NULL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to open plugin: %s",
			     g_module_error ());
		return FALSE;
	}

	/* set automatically */
	str = g_strstr_len (filename, -1, "libfu_plugin_");
	if (str != NULL) {
		priv->name = g_strdup (str + 13);
		g_strdelimit (priv->name, ".", '\0');
	}

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_init", (gpointer *) &func);
	if (func != NULL) {
		g_debug ("performing init() on %s", filename);
		func (plugin);
	}

	return TRUE;
}

/**
 * fu_plugin_device_add:
 * @plugin: A #FuPlugin
 * @device: A #FuDevice
 *
 * Asks the daemon to add a device to the exported list. If this device ID
 * has already been added by a different plugin then this request will be
 * ignored.
 *
 * Plugins should use fu_plugin_device_add_delay() if they are not capable of
 * actually flashing an image to the hardware so that higher-priority plugins
 * can add the device themselves.
 *
 * Since: 0.8.0
 **/
void
fu_plugin_device_add (FuPlugin *plugin, FuDevice *device)
{
	g_return_if_fail (FU_IS_PLUGIN (plugin));
	g_return_if_fail (FU_IS_DEVICE (device));

	g_debug ("emit added from %s: %s",
		 fu_plugin_get_name (plugin),
		 fu_device_get_id (device));
	fu_device_set_created (device, (guint64) g_get_real_time () / G_USEC_PER_SEC);
	fu_device_set_plugin (device, fu_plugin_get_name (plugin));
	g_signal_emit (plugin, signals[SIGNAL_DEVICE_ADDED], 0, device);
}

typedef struct {
	FuPlugin	*plugin;
	FuDevice	*device;
	guint		 timeout_id;
	GHashTable	*devices;
} FuPluginHelper;

static void
fu_plugin_helper_free (FuPluginHelper *helper)
{
	g_object_unref (helper->plugin);
	g_object_unref (helper->device);
	g_hash_table_unref (helper->devices);
	g_free (helper);
}

static gboolean
fu_plugin_device_add_delay_cb (gpointer user_data)
{
	FuPluginHelper *helper = (FuPluginHelper *) user_data;
	g_hash_table_remove (helper->devices, helper->device);
	fu_plugin_device_add (helper->plugin, helper->device);
	fu_plugin_helper_free (helper);
	return FALSE;
}

/**
 * fu_plugin_has_device_delay:
 * @plugin: A #FuPlugin
 *
 * Returns if the device has a pending device that is waiting to be added.
 *
 * Returns: %TRUE if a device is waiting to be added
 *
 * Since: 0.8.0
 **/
gboolean
fu_plugin_has_device_delay (FuPlugin *plugin)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	return g_hash_table_size (priv->devices_delay) > 0;
}

/**
 * fu_plugin_device_add_delay:
 * @plugin: A #FuPlugin
 * @device: A #FuDevice
 *
 * Asks the daemon to add a device to the exported list after a small delay.
 *
 * Since: 0.8.0
 **/
void
fu_plugin_device_add_delay (FuPlugin *plugin, FuDevice *device)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginHelper *helper;

	g_return_if_fail (FU_IS_PLUGIN (plugin));
	g_return_if_fail (FU_IS_DEVICE (device));

	/* already waiting for add */
	helper = g_hash_table_lookup (priv->devices_delay, device);
	if (helper != NULL) {
		g_warning ("ignoring add-delay as device %s already pending",
			   fu_device_get_id (device));
		return;
	}

	/* add after a small delay */
	g_debug ("waiting a small time for other plugins");
	helper = g_new0 (FuPluginHelper, 1);
	helper->plugin = g_object_ref (plugin);
	helper->device = g_object_ref (device);
	helper->timeout_id = g_timeout_add (500, fu_plugin_device_add_delay_cb, helper);
	helper->devices = g_hash_table_ref (priv->devices_delay);
	g_hash_table_insert (helper->devices, device, helper);
}

/**
 * fu_plugin_device_add:
 * @plugin: A #FuPlugin
 * @device: A #FuDevice
 *
 * Asks the daemon to remove a device from the exported list.
 *
 * Since: 0.8.0
 **/
void
fu_plugin_device_remove (FuPlugin *plugin, FuDevice *device)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginHelper *helper;

	g_return_if_fail (FU_IS_PLUGIN (plugin));
	g_return_if_fail (FU_IS_DEVICE (device));

	/* waiting for add */
	helper = g_hash_table_lookup (priv->devices_delay, device);
	if (helper != NULL) {
		g_debug ("ignoring remove from delayed addition");
		g_source_remove (helper->timeout_id);
		g_hash_table_remove (priv->devices_delay, helper->device);
		fu_plugin_helper_free (helper);
		return;
	}

	g_debug ("emit removed from %s: %s",
		 fu_plugin_get_name (plugin),
		 fu_device_get_id (device));
	g_signal_emit (plugin, signals[SIGNAL_DEVICE_REMOVED], 0, device);
}

/**
 * fu_plugin_set_status:
 * @plugin: A #FuPlugin
 * @status: A #FwupdStatus, e.g. #FWUPD_STATUS_DECOMPRESSING
 *
 * Sets the global state of the daemon according to the current plugin action.
 *
 * Since: 0.8.0
 **/
void
fu_plugin_set_status (FuPlugin *plugin, FwupdStatus status)
{
	g_return_if_fail (FU_IS_PLUGIN (plugin));
	g_signal_emit (plugin, signals[SIGNAL_STATUS_CHANGED], 0, status);
}

/**
 * fu_plugin_set_percentage:
 * @plugin: A #FuPlugin
 * @percentage: the percentage complete
 *
 * Sets the global completion of the daemon according to the current plugin
 * action.
 *
 * Since: 0.8.0
 **/
void
fu_plugin_set_percentage (FuPlugin *plugin, guint percentage)
{
	g_return_if_fail (FU_IS_PLUGIN (plugin));
	g_return_if_fail (percentage <= 100);
	g_signal_emit (plugin, signals[SIGNAL_PERCENTAGE_CHANGED], 0,
		       percentage);
}

/**
 * fu_plugin_recoldplug:
 * @plugin: A #FuPlugin
 *
 * Ask all the plugins to coldplug all devices, which will include the prepare()
 * and cleanup() phases. Duplicate devices added will be ignored.
 *
 * Since: 0.8.0
 **/
void
fu_plugin_recoldplug (FuPlugin *plugin)
{
	g_return_if_fail (FU_IS_PLUGIN (plugin));
	g_signal_emit (plugin, signals[SIGNAL_RECOLDPLUG], 0);
}

/**
 * fu_plugin_set_coldplug_delay:
 * @plugin: A #FuPlugin
 * @duration: A delay in milliseconds
 *
 * Set the minimum time that should be waited inbetween the call to
 * fu_plugin_coldplug_prepare() and fu_plugin_coldplug(). This is usually going
 * to be the minimum hardware initialisation time from a datasheet.
 *
 * It is better to use this function rather than using a sleep() in the plugin
 * itself as then only one delay is done in the daemon rather than waiting for
 * each coldplug prepare in a serial way.
 *
 * Additionally, very long delays should be avoided as the daemon will be
 * blocked from processing requests whilst the coldplug delay is being
 * performed.
 *
 * Since: 0.8.0
 **/
void
fu_plugin_set_coldplug_delay (FuPlugin *plugin, guint duration)
{
	g_return_if_fail (FU_IS_PLUGIN (plugin));
	g_return_if_fail (duration > 0);

	/* check sanity */
	if (duration > FU_PLUGIN_COLDPLUG_DELAY_MAXIMUM) {
		g_warning ("duration of %ums is crazy, truncating to %ums",
			   duration,
			   FU_PLUGIN_COLDPLUG_DELAY_MAXIMUM);
		duration = FU_PLUGIN_COLDPLUG_DELAY_MAXIMUM;
	}

	/* emit */
	g_signal_emit (plugin, signals[SIGNAL_SET_COLDPLUG_DELAY], 0, duration);
}

gboolean
fu_plugin_runner_startup (FuPlugin *plugin, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginStartupFunc func = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_startup", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("performing startup() on %s", priv->name);
	if (!func (plugin, error)) {
		g_prefix_error (error, "failed to startup %s: ", priv->name);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_plugin_runner_offline_invalidate (GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GFile) file1 = NULL;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	file1 = g_file_new_for_path (FU_OFFLINE_TRIGGER_FILENAME);
	if (!g_file_query_exists (file1, NULL))
		return TRUE;
	if (!g_file_delete (file1, NULL, &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Cannot delete %s: %s",
			     FU_OFFLINE_TRIGGER_FILENAME,
			     error_local->message);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_plugin_runner_offline_setup (GError **error)
{
	gint rc;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* create symlink for the systemd-system-update-generator */
	rc = symlink ("/var/lib/fwupd", FU_OFFLINE_TRIGGER_FILENAME);
	if (rc < 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "Failed to create symlink %s to %s: %s",
			     FU_OFFLINE_TRIGGER_FILENAME,
			     "/var/lib", strerror (errno));
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_runner_coldplug (FuPlugin *plugin, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginStartupFunc func = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_coldplug", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("performing coldplug() on %s", priv->name);
	if (!func (plugin, error)) {
		g_prefix_error (error, "failed to coldplug %s: ", priv->name);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_runner_coldplug_prepare (FuPlugin *plugin, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginStartupFunc func = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_coldplug_prepare", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("performing coldplug_prepare() on %s", priv->name);
	if (!func (plugin, error)) {
		g_prefix_error (error, "failed to prepare for coldplug %s: ", priv->name);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_runner_coldplug_cleanup (FuPlugin *plugin, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginStartupFunc func = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_coldplug_cleanup", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("performing coldplug_cleanup() on %s", priv->name);
	if (!func (plugin, error)) {
		g_prefix_error (error, "failed to cleanup coldplug %s: ", priv->name);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_runner_update_prepare (FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginDeviceFunc func = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_update_prepare", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("performing update_prepare() on %s", priv->name);
	if (!func (plugin, device, error)) {
		g_prefix_error (error, "failed to prepare for update %s: ", priv->name);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_runner_update_cleanup (FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginDeviceFunc func = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_update_cleanup", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("performing update_cleanup() on %s", priv->name);
	if (!func (plugin, device, error)) {
		g_prefix_error (error, "failed to cleanup update %s: ", priv->name);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_plugin_runner_schedule_update (FuPlugin *plugin,
			     FuDevice *device,
			     GBytes *blob_cab,
			     GError **error)
{
	gchar tmpname[] = {"XXXXXX.cap"};
	g_autofree gchar *dirname = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FwupdResult) res_tmp = NULL;
	g_autoptr(FuPending) pending = NULL;
	g_autoptr(GFile) file = NULL;

	/* id already exists */
	pending = fu_pending_new ();
	res_tmp = fu_pending_get_device (pending, fu_device_get_id (device), NULL);
	if (res_tmp != NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_ALREADY_PENDING,
			     "%s is already scheduled to be updated",
			     fu_device_get_id (device));
		return FALSE;
	}

	/* create directory */
	dirname = g_build_filename (LOCALSTATEDIR, "lib", "fwupd", NULL);
	file = g_file_new_for_path (dirname);
	if (!g_file_query_exists (file, NULL)) {
		if (!g_file_make_directory_with_parents (file, NULL, error))
			return FALSE;
	}

	/* get a random filename */
	for (guint i = 0; i < 6; i++)
		tmpname[i] = (gchar) g_random_int_range ('A', 'Z');
	filename = g_build_filename (dirname, tmpname, NULL);

	/* just copy to the temp file */
	fu_plugin_set_status (plugin, FWUPD_STATUS_SCHEDULING);
	if (!g_file_set_contents (filename,
				  g_bytes_get_data (blob_cab, NULL),
				  (gssize) g_bytes_get_size (blob_cab),
				  error))
		return FALSE;

	/* schedule for next boot */
	g_debug ("schedule %s to be installed to %s on next boot",
		 filename, fu_device_get_id (device));
	fu_device_set_update_filename (device, filename);

	/* add to database */
	if (!fu_pending_add_device (pending, FWUPD_RESULT (device), error))
		return FALSE;

	/* next boot we run offline */
	return fu_plugin_runner_offline_setup (error);
}

gboolean
fu_plugin_runner_verify (FuPlugin *plugin,
			 FuDevice *device,
			 FuPluginVerifyFlags flags,
			 GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginVerifyFunc func = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_verify", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("performing verify() on %s", priv->name);
	if (!func (plugin, device, flags, error)) {
		g_prefix_error (error, "failed to verify %s: ", priv->name);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_runner_unlock (FuPlugin *plugin, FuDevice *device, GError **error)
{
	guint64 flags;
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginDeviceFunc func = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* final check */
	flags = fu_device_get_flags (device);
	if ((flags & FWUPD_DEVICE_FLAG_LOCKED) == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Device %s is not locked",
			     fu_device_get_id (device));
		return FALSE;
	}

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_unlock", (gpointer *) &func);
	if (func != NULL) {
		g_debug ("performing unlock() on %s", priv->name);
		if (!func (plugin, device, error)) {
			g_prefix_error (error, "failed to unlock %s: ", priv->name);
			return FALSE;
		}
	}

	/* update with correct flags */
	flags = fu_device_get_flags (device);
	fu_device_set_flags (device, flags &= ~FWUPD_DEVICE_FLAG_LOCKED);
	fu_device_set_modified (device, (guint64) g_get_real_time () / G_USEC_PER_SEC);
	return TRUE;
}

gboolean
fu_plugin_runner_update (FuPlugin *plugin,
		    FuDevice *device,
		    GBytes *blob_cab,
		    GBytes *blob_fw,
		    FwupdInstallFlags flags,
		    GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginUpdateFunc func_online;
	FuPluginUpdateFunc func_offline;
	g_autoptr(FuPending) pending = NULL;
	g_autoptr(FwupdResult) res_pending = NULL;
	GError *error_update = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_update_online", (gpointer *) &func_online);
	g_module_symbol (priv->module, "fu_plugin_update_offline", (gpointer *) &func_offline);

	/* schedule for next reboot, or handle in the plugin */
	if (flags & FWUPD_INSTALL_FLAG_OFFLINE) {
		if (func_offline == NULL) {
			return fu_plugin_runner_schedule_update (plugin,
								 device,
								 blob_cab,
								 error);
		}
		return func_offline (plugin, device, blob_fw, flags, error);
	}

	/* cancel the pending action */
	if (!fu_plugin_runner_offline_invalidate (error))
		return FALSE;

	/* online */
	if (func_online == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "No online update possible");
		return FALSE;
	}
	pending = fu_pending_new ();
	res_pending = fu_pending_get_device (pending, fu_device_get_id (device), NULL);
	if (!func_online (plugin, device, blob_fw, flags, &error_update)) {
		/* save the error to the database */
		if (res_pending != NULL) {
			fu_pending_set_error_msg (pending, FWUPD_RESULT (device),
						  error_update->message, NULL);
		}
		g_propagate_error (error, error_update);
		return FALSE;
	}

	/* cleanup */
	if (res_pending != NULL) {
		const gchar *tmp;

		/* update pending database */
		fu_pending_set_state (pending, FWUPD_RESULT (device),
				      FWUPD_UPDATE_STATE_SUCCESS, NULL);

		/* delete cab file */
		tmp = fwupd_result_get_update_filename (res_pending);
		if (tmp != NULL && g_str_has_prefix (tmp, LIBEXECDIR)) {
			g_autoptr(GError) error_local = NULL;
			g_autoptr(GFile) file = NULL;
			file = g_file_new_for_path (tmp);
			if (!g_file_delete (file, NULL, &error_local)) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "Failed to delete %s: %s",
					     tmp, error_local->message);
				return FALSE;
			}
		}
	}
	return TRUE;
}

gboolean
fu_plugin_runner_clear_results (FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginDeviceFunc func = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FwupdResult) res_pending = NULL;
	g_autoptr(FuPending) pending = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* use the plugin if the vfunc is provided */
	g_module_symbol (priv->module, "fu_plugin_clear_result", (gpointer *) &func);
	if (func != NULL) {
		g_debug ("performing clear_result() on %s", priv->name);
		if (!func (plugin, device, error)) {
			g_prefix_error (error, "failed to clear_result %s: ", priv->name);
			return FALSE;
		}
		return TRUE;
	}

	/* handled using the database */
	pending = fu_pending_new ();
	res_pending = fu_pending_get_device (pending,
					     fu_device_get_id (device),
					     &error_local);
	if (res_pending == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to find %s in pending database: %s",
			     fu_device_get_id (device),
			     error_local->message);
		return FALSE;
	}

	/* remove from pending database */
	return fu_pending_remove_device (pending, FWUPD_RESULT (device), error);
}

gboolean
fu_plugin_runner_get_results (FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginDeviceFunc func = NULL;
	FwupdUpdateState update_state;
	const gchar *tmp;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FwupdResult) res_pending = NULL;
	g_autoptr(FuPending) pending = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* use the plugin if the vfunc is provided */
	g_module_symbol (priv->module, "fu_plugin_get_results", (gpointer *) &func);
	if (func != NULL) {
		g_debug ("performing get_results() on %s", priv->name);
		if (!func (plugin, device, error)) {
			g_prefix_error (error, "failed to get_results %s: ", priv->name);
			return FALSE;
		}
		return TRUE;
	}

	/* handled using the database */
	pending = fu_pending_new ();
	res_pending = fu_pending_get_device (pending,
					     fu_device_get_id (device),
					     &error_local);
	if (res_pending == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOTHING_TO_DO,
			     "Failed to find %s in pending database: %s",
			     fu_device_get_id (device),
			     error_local->message);
		return FALSE;
	}

	/* copy the important parts from the pending device to the real one */
	update_state = fwupd_result_get_update_state (res_pending);
	if (update_state == FWUPD_UPDATE_STATE_UNKNOWN ||
	    update_state == FWUPD_UPDATE_STATE_PENDING) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOTHING_TO_DO,
			     "Device %s has not been updated offline yet",
			     fu_device_get_id (device));
		return FALSE;
	}

	/* copy */
	fu_device_set_update_state (device, update_state);
	tmp = fwupd_result_get_update_error (res_pending);
	if (tmp != NULL)
		fu_device_set_update_error (device, tmp);
	tmp = fwupd_result_get_device_version (res_pending);
	if (tmp != NULL)
		fu_device_set_version (device, tmp);
	tmp = fwupd_result_get_update_version (res_pending);
	if (tmp != NULL)
		fu_device_set_update_version (device, tmp);
	return TRUE;
}

static void
fu_plugin_class_init (FuPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_plugin_finalize;
	signals[SIGNAL_DEVICE_ADDED] =
		g_signal_new ("device-added",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuPluginClass, device_added),
			      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);
	signals[SIGNAL_DEVICE_REMOVED] =
		g_signal_new ("device-removed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuPluginClass, device_removed),
			      NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1, FU_TYPE_DEVICE);
	signals[SIGNAL_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuPluginClass, status_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals[SIGNAL_PERCENTAGE_CHANGED] =
		g_signal_new ("percentage-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuPluginClass, percentage_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals[SIGNAL_RECOLDPLUG] =
		g_signal_new ("recoldplug",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuPluginClass, recoldplug),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals[SIGNAL_SET_COLDPLUG_DELAY] =
		g_signal_new ("set-coldplug-delay",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuPluginClass, set_coldplug_delay),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
}

static void
fu_plugin_init (FuPlugin *plugin)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	priv->enabled = TRUE;
	priv->devices = g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free, (GDestroyNotify) g_object_unref);
	priv->devices_delay = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
fu_plugin_finalize (GObject *object)
{
	FuPlugin *plugin = FU_PLUGIN (object);
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginInitFunc func = NULL;

	/* optional */
	if (priv->module != NULL) {
		g_module_symbol (priv->module, "fu_plugin_destroy", (gpointer *) &func);
		if (func != NULL) {
			g_debug ("performing destroy() on %s", priv->name);
			func (plugin);
		}
	}

	if (priv->usb_ctx != NULL)
		g_object_unref (priv->usb_ctx);
#ifndef RUNNING_ON_VALGRIND
	if (priv->module != NULL)
		g_module_close (priv->module);
#endif
	g_hash_table_unref (priv->devices);
	g_hash_table_unref (priv->devices_delay);
	g_free (priv->name);
	g_free (priv->data);

	G_OBJECT_CLASS (fu_plugin_parent_class)->finalize (object);
}

FuPlugin *
fu_plugin_new (void)
{
	FuPlugin *plugin;
	plugin = g_object_new (FU_TYPE_PLUGIN, NULL);
	return plugin;
}

GChecksumType
fu_plugin_get_checksum_type (FuPluginVerifyFlags flags)
{
	if (flags & FU_PLUGIN_VERIFY_FLAG_USE_SHA256)
		return G_CHECKSUM_SHA256;
	return G_CHECKSUM_SHA1;
}

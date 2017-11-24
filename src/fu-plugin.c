/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
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

#include "fu-device-private.h"
#include "fu-plugin-private.h"
#include "fu-pending.h"

/**
 * SECTION:fu-plugin
 * @short_description: a daemon plugin
 *
 * An object that represents a plugin run by the daemon.
 *
 * See also: #FuDevice
 */

#define	FU_PLUGIN_COLDPLUG_DELAY_MAXIMUM	3000u	/* ms */

static void fu_plugin_finalize			 (GObject *object);

typedef struct {
	GModule			*module;
	GUsbContext		*usb_ctx;
	gboolean		 enabled;
	guint			 order;
	GPtrArray		*rules[FU_PLUGIN_RULE_LAST];
	gchar			*name;
	FuHwids			*hwids;
	FuQuirks		*quirks;
	GPtrArray		*supported_guids;
	FuSmbios		*smbios;
	GHashTable		*devices;	/* platform_id:GObject */
	GHashTable		*devices_delay;	/* FuDevice:FuPluginHelper */
	FuPluginData		*data;
} FuPluginPrivate;

enum {
	SIGNAL_DEVICE_ADDED,
	SIGNAL_DEVICE_REMOVED,
	SIGNAL_DEVICE_REGISTER,
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
typedef void		 (*FuPluginDeviceRegisterFunc)	(FuPlugin	*plugin,
							 FuDevice	*device);
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
typedef gboolean	 (*FuPluginUsbDeviceAddedFunc)	(FuPlugin	*plugin,
							 GUsbDevice	*usb_device,
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

void
fu_plugin_set_name (FuPlugin *plugin, const gchar *name)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_if_fail (FU_IS_PLUGIN (plugin));
	g_return_if_fail (name != NULL);
	g_free (priv->name);
	priv->name = g_strdup (name);
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
 * Gets the per-plugin allocated private data. This will return %NULL unless
 * fu_plugin_alloc_data() has been called by the plugin.
 *
 * Returns: (transfer none): a pointer to a structure, or %NULL for unset.
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
 * Returns if the plugin is enabled. Plugins may self-disable using
 * fu_plugin_set_enabled() or can be disabled by the daemon.
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

/**
 * fu_plugin_device_register:
 * @plugin: A #FuPlugin
 * @device: A #FuDevice
 *
 * Registers the device with other plugins so they can set metadata.
 *
 * Plugins do not have to call this manually as this is done automatically
 * when using fu_plugin_device_add(). They may wish to use this manually
 * if for intance the coldplug should be ignored based on the metadata
 * set from other plugins.
 *
 * Since: 0.9.7
 **/
void
fu_plugin_device_register (FuPlugin *plugin, FuDevice *device)
{
	g_return_if_fail (FU_IS_PLUGIN (plugin));
	g_return_if_fail (FU_IS_DEVICE (device));

	g_debug ("emit device-register from %s: %s",
		 fu_plugin_get_name (plugin),
		 fu_device_get_id (device));
	g_signal_emit (plugin, signals[SIGNAL_DEVICE_REGISTER], 0, device);
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
	fu_plugin_device_add (helper->plugin, helper->device);
	g_hash_table_remove (helper->devices, helper->device);
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
		g_debug ("ignoring add-delay as device %s already pending",
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
 * fu_plugin_device_remove:
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
 * fu_plugin_check_hwid:
 * @plugin: A #FuPlugin
 * @hwid: A Hardware ID GUID, e.g. `6de5d951-d755-576b-bd09-c5cf66b27234`
 *
 * Checks to see if a specific GUID exists. All hardware IDs on a
 * specific system can be shown using the `fwupdmgr hwids` command.
 *
 * Returns: %TRUE if the HwId is found on the system.
 *
 * Since: 0.9.1
 **/
gboolean
fu_plugin_check_hwid (FuPlugin *plugin, const gchar *hwid)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	if (priv->hwids == NULL)
		return FALSE;
	return fu_hwids_has_guid (priv->hwids, hwid);
}

/**
 * fu_plugin_check_supported:
 * @plugin: A #FuPlugin
 * @guid: A Hardware ID GUID, e.g. `6de5d951-d755-576b-bd09-c5cf66b27234`
 *
 * Checks to see if a specific device GUID is supported, i.e. available in the
 * AppStream metadata.
 *
 * Returns: %TRUE if the device is supported.
 *
 * Since: 1.0.0
 **/
gboolean
fu_plugin_check_supported (FuPlugin *plugin, const gchar *guid)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	if (priv->supported_guids == NULL)
		return FALSE;
	for (guint i = 0; i < priv->supported_guids->len; i++) {
		const gchar *guid_tmp = g_ptr_array_index (priv->supported_guids, i);
		if (g_strcmp0 (guid, guid_tmp) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fu_plugin_get_dmi_value:
 * @plugin: A #FuPlugin
 * @dmi_id: A DMI ID, e.g. `BiosVersion`
 *
 * Gets a hardware DMI value.
 *
 * Returns: The string, or %NULL
 *
 * Since: 0.9.7
 **/
const gchar *
fu_plugin_get_dmi_value (FuPlugin *plugin, const gchar *dmi_id)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	if (priv->hwids == NULL)
		return NULL;
	return fu_hwids_get_value (priv->hwids, dmi_id);
}

/**
 * fu_plugin_get_smbios_string:
 * @plugin: A #FuPlugin
 * @structure_type: A SMBIOS structure type, e.g. %FU_SMBIOS_STRUCTURE_TYPE_BIOS
 * @offset: A SMBIOS offset
 *
 * Gets a hardware SMBIOS string.
 *
 * The @type and @offset can be referenced from the DMTF SMBIOS specification:
 * https://www.dmtf.org/sites/default/files/standards/documents/DSP0134_3.1.1.pdf
 *
 * Returns: A string, or %NULL
 *
 * Since: 0.9.8
 **/
const gchar *
fu_plugin_get_smbios_string (FuPlugin *plugin, guint8 structure_type, guint8 offset)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	if (priv->smbios == NULL)
		return NULL;
	return fu_smbios_get_string (priv->smbios, structure_type, offset, NULL);
}

/**
 * fu_plugin_get_smbios_data:
 * @plugin: A #FuPlugin
 * @structure_type: A SMBIOS structure type, e.g. %FU_SMBIOS_STRUCTURE_TYPE_BIOS
 *
 * Gets a hardware SMBIOS data.
 *
 * Returns: (transfer none): A #GBytes, or %NULL
 *
 * Since: 0.9.8
 **/
GBytes *
fu_plugin_get_smbios_data (FuPlugin *plugin, guint8 structure_type)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	if (priv->smbios == NULL)
		return NULL;
	return fu_smbios_get_data (priv->smbios, structure_type, NULL);
}

void
fu_plugin_set_hwids (FuPlugin *plugin, FuHwids *hwids)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	g_set_object (&priv->hwids, hwids);
}

void
fu_plugin_set_supported (FuPlugin *plugin, GPtrArray *supported_guids)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	if (priv->supported_guids != NULL)
		g_ptr_array_unref (priv->supported_guids);
	priv->supported_guids = g_ptr_array_ref (supported_guids);
}

void
fu_plugin_set_quirks (FuPlugin *plugin, FuQuirks *quirks)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	g_set_object (&priv->quirks, quirks);
}

/**
 * fu_plugin_get_quirks:
 * @plugin: A #FuPlugin
 *
 * Returns the hardware database object. This can be used to discover device
 * quirks or other device-specific settings.
 *
 * Returns: (transfer none): a #FuQuirks, or %NULL if not set
 *
 * Since: 1.0.1
 **/
FuQuirks *
fu_plugin_get_quirks (FuPlugin *plugin)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	return priv->quirks;
}

/**
 * fu_plugin_lookup_quirk_by_id:
 * @plugin: A #FuPlugin
 * @prefix: A string prefix that matches the quirks file basename, e.g. "dfu-quirks"
 * @id: An ID to match the entry, e.g. "012345"
 *
 * Looks up an entry in the hardware database using a string value.
 *
 * Returns: (transfer none): values from the database, or %NULL if not found
 *
 * Since: 1.0.1
 **/
const gchar *
fu_plugin_lookup_quirk_by_id (FuPlugin *plugin, const gchar *prefix, const gchar *id)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_val_if_fail (FU_IS_PLUGIN (plugin), NULL);

	/* wildcard */
	if (g_strstr_len (id, -1, "*") != NULL)
		return fu_quirks_lookup_by_glob (priv->quirks, prefix, id);

	/* exact ID */
	return fu_quirks_lookup_by_id (priv->quirks, prefix, id);
}

/**
 * fu_plugin_lookup_quirk_by_usb_device:
 * @plugin: A #FuPlugin
 * @prefix: A string prefix that matches the quirks file basename, e.g. "dfu-quirks"
 * @dev: A #GUsbDevice
 *
 * Looks up an entry in the hardware database using various keys generated
 * from @dev.
 *
 * Returns: (transfer none): values from the database, or %NULL if not found
 *
 * Since: 1.0.1
 **/
const gchar *
fu_plugin_lookup_quirk_by_usb_device (FuPlugin *plugin, const gchar *prefix, GUsbDevice *dev)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_val_if_fail (FU_IS_PLUGIN (plugin), NULL);
	return fu_quirks_lookup_by_usb_device (priv->quirks, prefix, dev);
}

/**
 * fu_plugin_get_supported:
 * @plugin: A #FuPlugin
 *
 * Gets all the device GUIDs supported by the daemon.
 *
 * Returns: (element-type utf8) (transfer none): GUIDs
 *
 * Since: 1.0.0
 **/
GPtrArray *
fu_plugin_get_supported (FuPlugin *plugin)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	return priv->supported_guids;
}

void
fu_plugin_set_smbios (FuPlugin *plugin, FuSmbios *smbios)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	g_set_object (&priv->smbios, smbios);
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

static gboolean
fu_plugin_runner_device_generic (FuPlugin *plugin, FuDevice *device,
				 const gchar *symbol_name, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginDeviceFunc func = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, symbol_name, (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("performing %s() on %s", symbol_name + 10, priv->name);
	if (!func (plugin, device, error)) {
		g_prefix_error (error, "failed to run %s(%s) on %s: ",
				symbol_name + 10,
				fu_device_get_id (device),
				priv->name);
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
	return fu_plugin_runner_device_generic (plugin, device,
						"fu_plugin_update_prepare", error);
}

gboolean
fu_plugin_runner_update_cleanup (FuPlugin *plugin, FuDevice *device, GError **error)
{
	return fu_plugin_runner_device_generic (plugin, device,
						"fu_plugin_update_cleanup", error);
}

gboolean
fu_plugin_runner_update_attach (FuPlugin *plugin, FuDevice *device, GError **error)
{
	return fu_plugin_runner_device_generic (plugin, device,
						"fu_plugin_update_attach", error);
}

gboolean
fu_plugin_runner_update_detach (FuPlugin *plugin, FuDevice *device, GError **error)
{
	return fu_plugin_runner_device_generic (plugin, device,
						"fu_plugin_update_detach", error);
}

gboolean
fu_plugin_runner_update_reload (FuPlugin *plugin, FuDevice *device, GError **error)
{
	return fu_plugin_runner_device_generic (plugin, device,
						"fu_plugin_update_reload", error);
}

gboolean
fu_plugin_runner_usb_device_added (FuPlugin *plugin, GUsbDevice *usb_device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginUsbDeviceAddedFunc func = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_usb_device_added", (gpointer *) &func);
	if (func != NULL) {
		g_debug ("performing usb_device_added() on %s", priv->name);
		return func (plugin, usb_device, error);
	}
	return TRUE;
}

void
fu_plugin_runner_device_register (FuPlugin *plugin, FuDevice *device)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginDeviceRegisterFunc func = NULL;

	/* not enabled */
	if (!priv->enabled)
		return;
	if (priv->module == NULL)
		return;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_device_registered", (gpointer *) &func);
	if (func != NULL) {
		g_debug ("performing device_added() on %s", priv->name);
		func (plugin, device);
	}
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
	g_autoptr(FuDevice) res_tmp = NULL;
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
	fu_device_set_filename_pending (device, filename);

	/* add to database */
	if (!fu_pending_add_device (pending, device, error))
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
	GPtrArray *checksums;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* clear any existing verification checksums */
	checksums = fu_device_get_checksums (device);
	g_ptr_array_set_size (checksums, 0);

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

	/* run vfunc */
	if (!fu_plugin_runner_device_generic (plugin, device,
					      "fu_plugin_unlock", error))
		return FALSE;

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
	FuPluginUpdateFunc update_func;
	g_autoptr(FuPending) pending = NULL;
	g_autoptr(FuDevice) device_pending = NULL;
	GError *error_update = NULL;
	GPtrArray *checksums;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_update", (gpointer *) &update_func);
	if (update_func == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "No update possible");
		return FALSE;
	}

	/* just schedule this for the next reboot  */
	if (flags & FWUPD_INSTALL_FLAG_OFFLINE) {
		return fu_plugin_runner_schedule_update (plugin,
							 device,
							 blob_cab,
							 error);
	}

	/* cancel the pending action */
	if (!fu_plugin_runner_offline_invalidate (error))
		return FALSE;

	/* online */
	pending = fu_pending_new ();
	device_pending = fu_pending_get_device (pending, fu_device_get_id (device), NULL);
	if (!update_func (plugin, device, blob_fw, flags, &error_update)) {
		/* save the error to the database */
		if (device_pending != NULL) {
			fu_pending_set_error_msg (pending, device,
						  error_update->message, NULL);
		}
		g_propagate_error (error, error_update);
		return FALSE;
	}

	/* no longer valid */
	checksums = fu_device_get_checksums (device);
	g_ptr_array_set_size (checksums, 0);

	/* cleanup */
	if (device_pending != NULL) {
		const gchar *tmp;

		/* update pending database */
		fu_pending_set_state (pending, device,
				      FWUPD_UPDATE_STATE_SUCCESS, NULL);

		/* delete cab file */
		tmp = fu_device_get_filename_pending (device_pending);
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
	g_autoptr(FuDevice) device_pending = NULL;
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
	device_pending = fu_pending_get_device (pending,
					     fu_device_get_id (device),
					     &error_local);
	if (device_pending == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INVALID_FILE,
			     "Failed to find %s in pending database: %s",
			     fu_device_get_id (device),
			     error_local->message);
		return FALSE;
	}

	/* remove from pending database */
	return fu_pending_remove_device (pending, device, error);
}

gboolean
fu_plugin_runner_get_results (FuPlugin *plugin, FuDevice *device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginDeviceFunc func = NULL;
	FwupdUpdateState update_state;
	const gchar *tmp;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(FuDevice) device_pending = NULL;
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
	device_pending = fu_pending_get_device (pending,
					     fu_device_get_id (device),
					     &error_local);
	if (device_pending == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOTHING_TO_DO,
			     "Failed to find %s in pending database: %s",
			     fu_device_get_id (device),
			     error_local->message);
		return FALSE;
	}

	/* copy the important parts from the pending device to the real one */
	update_state = fu_device_get_update_state (device_pending);
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
	tmp = fu_device_get_update_error (device_pending);
	if (tmp != NULL)
		fu_device_set_update_error (device, tmp);
	tmp = fu_device_get_version (device_pending);
	if (tmp != NULL)
		fu_device_set_version (device, tmp);
	tmp = fu_device_get_version_new (device_pending);
	if (tmp != NULL)
		fu_device_set_version_new (device, tmp);
	return TRUE;
}

/**
 * fu_plugin_get_order:
 * @plugin: a #FuPlugin
 *
 * Gets the plugin order, where higher numbers are run after lower
 * numbers.
 *
 * Returns: the integer value
 **/
guint
fu_plugin_get_order (FuPlugin *plugin)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (plugin);
	return priv->order;
}

/**
 * fu_plugin_set_order:
 * @plugin: a #FuPlugin
 * @order: a integer value
 *
 * Sets the plugin order, where higher numbers are run after lower
 * numbers.
 **/
void
fu_plugin_set_order (FuPlugin *plugin, guint order)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (plugin);
	priv->order = order;
}

/**
 * fu_plugin_add_rule:
 * @plugin: a #FuPlugin
 * @rule: a #FuPluginRule, e.g. %FU_PLUGIN_RULE_CONFLICTS
 * @name: a plugin name, e.g. `upower`
 *
 * If the plugin name is found, the rule will be used to sort the plugin list,
 * for example the plugin specified by @name will be ordered after this plugin
 * when %FU_PLUGIN_RULE_RUN_AFTER is used.
 *
 * NOTE: The depsolver is iterative and may not solve overly-complicated rules;
 * If depsolving fails then fwupd will not start.
 **/
void
fu_plugin_add_rule (FuPlugin *plugin, FuPluginRule rule, const gchar *name)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (plugin);
	g_ptr_array_add (priv->rules[rule], g_strdup (name));
}

/**
 * fu_plugin_get_rules:
 * @plugin: a #FuPlugin
 * @rule: a #FuPluginRule, e.g. %FU_PLUGIN_RULE_CONFLICTS
 *
 * Gets the plugin IDs that should be run after this plugin.
 *
 * Returns: (element-type utf8) (transfer none): the list of plugin names, e.g. ['appstream']
 **/
GPtrArray *
fu_plugin_get_rules (FuPlugin *plugin, FuPluginRule rule)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (plugin);
	return priv->rules[rule];
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
	signals[SIGNAL_DEVICE_REGISTER] =
		g_signal_new ("device-register",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuPluginClass, device_register),
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
	priv->devices_delay = g_hash_table_new (g_direct_hash, g_direct_equal);
	for (guint i = 0; i < FU_PLUGIN_RULE_LAST; i++)
		priv->rules[i] = g_ptr_array_new_with_free_func (g_free);
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

	for (guint i = 0; i < FU_PLUGIN_RULE_LAST; i++)
		g_ptr_array_unref (priv->rules[i]);

	if (priv->usb_ctx != NULL)
		g_object_unref (priv->usb_ctx);
	if (priv->hwids != NULL)
		g_object_unref (priv->hwids);
	if (priv->quirks != NULL)
		g_object_unref (priv->quirks);
	if (priv->supported_guids != NULL)
		g_ptr_array_unref (priv->supported_guids);
	if (priv->smbios != NULL)
		g_object_unref (priv->smbios);
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

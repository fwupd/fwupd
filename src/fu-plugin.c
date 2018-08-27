/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupd.h>
#include <gmodule.h>
#include <appstream-glib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <gio/gunixinputstream.h>
#ifdef HAVE_VALGRIND
#include <valgrind.h>
#endif /* HAVE_VALGRIND */

#include "fu-device-private.h"
#include "fu-plugin-private.h"
#include "fu-history.h"

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
	guint			 priority;
	GPtrArray		*rules[FU_PLUGIN_RULE_LAST];
	gchar			*name;
	FuHwids			*hwids;
	FuQuirks		*quirks;
	GHashTable		*runtime_versions;
	GHashTable		*compile_versions;
	GPtrArray		*supported_guids;
	GPtrArray		*udev_subsystems;
	FuSmbios		*smbios;
	GHashTable		*devices;	/* platform_id:GObject */
	GHashTable		*devices_delay;	/* FuDevice:FuPluginHelper */
	GHashTable		*report_metadata;	/* key:value */
	FuPluginData		*data;
} FuPluginPrivate;

enum {
	SIGNAL_DEVICE_ADDED,
	SIGNAL_DEVICE_REMOVED,
	SIGNAL_DEVICE_REGISTER,
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
typedef gboolean	 (*FuPluginFlaggedDeviceFunc)	(FuPlugin	*plugin,
							 FwupdInstallFlags flags,
							 FuDevice	*device,
							 GError		**error);
typedef gboolean	 (*FuPluginDeviceArrayFunc)	(FuPlugin	*plugin,
							 GPtrArray	*devices,
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
typedef gboolean	 (*FuPluginUdevDeviceAddedFunc)	(FuPlugin	*plugin,
							 GUdevDevice	*udev_device,
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

gchar *
fu_plugin_guess_name_from_fn (const gchar *filename)
{
	const gchar *prefix = "libfu_plugin_";
	gchar *name;
	gchar *str = g_strstr_len (filename, -1, prefix);
	if (str == NULL)
		return NULL;
	name = g_strdup (str + strlen (prefix));
	g_strdelimit (name, ".", '\0');
	return name;
}

gboolean
fu_plugin_open (FuPlugin *plugin, const gchar *filename, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginInitFunc func = NULL;

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
	if (priv->name == NULL)
		priv->name = fu_plugin_guess_name_from_fn (filename);

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
	GPtrArray *children;

	g_return_if_fail (FU_IS_PLUGIN (plugin));
	g_return_if_fail (FU_IS_DEVICE (device));

	g_debug ("emit added from %s: %s",
		 fu_plugin_get_name (plugin),
		 fu_device_get_id (device));
	fu_device_set_created (device, (guint64) g_get_real_time () / G_USEC_PER_SEC);
	fu_device_set_plugin (device, fu_plugin_get_name (plugin));
	g_signal_emit (plugin, signals[SIGNAL_DEVICE_ADDED], 0, device);

	/* add children if they have not already been added */
	children = fu_device_get_children (device);
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index (children, i);
		if (fu_device_get_created (child) == 0)
			fu_plugin_device_add (plugin, child);
	}
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
 * fu_plugin_request_recoldplug:
 * @plugin: A #FuPlugin
 *
 * Ask all the plugins to coldplug all devices, which will include the prepare()
 * and cleanup() phases. Duplicate devices added will be ignored.
 *
 * Since: 0.8.0
 **/
void
fu_plugin_request_recoldplug (FuPlugin *plugin)
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
 * fu_plugin_get_hwids:
 * @plugin: A #FuPlugin
 *
 * Returns all the HWIDs defined in the system. All hardware IDs on a
 * specific system can be shown using the `fwupdmgr hwids` command.
 *
 * Returns: (transfer none) (element-type utf-8): An array of GUIDs
 *
 * Since: 1.1.1
 **/
GPtrArray *
fu_plugin_get_hwids (FuPlugin *plugin)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	if (priv->hwids == NULL)
		return NULL;
	return fu_hwids_get_guids (priv->hwids);
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
fu_plugin_set_udev_subsystems (FuPlugin *plugin, GPtrArray *udev_subsystems)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	if (priv->udev_subsystems != NULL)
		g_ptr_array_unref (priv->udev_subsystems);
	priv->udev_subsystems = g_ptr_array_ref (udev_subsystems);
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

void
fu_plugin_set_runtime_versions (FuPlugin *plugin, GHashTable *runtime_versions)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	priv->runtime_versions = g_hash_table_ref (runtime_versions);
}

/**
 * fu_plugin_add_runtime_version:
 * @plugin: A #FuPlugin
 * @component_id: An AppStream component id, e.g. "org.gnome.Software"
 * @version: A version string, e.g. "1.2.3"
 *
 * Sets a runtime version of a specific dependancy.
 *
 * Since: 1.0.7
 **/
void
fu_plugin_add_runtime_version (FuPlugin *plugin,
			       const gchar *component_id,
			       const gchar *version)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	if (priv->runtime_versions == NULL)
		return;
	g_hash_table_insert (priv->runtime_versions,
			     g_strdup (component_id),
			     g_strdup (version));
}

void
fu_plugin_set_compile_versions (FuPlugin *plugin, GHashTable *compile_versions)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	priv->compile_versions = g_hash_table_ref (compile_versions);
}

/**
 * fu_plugin_add_compile_version:
 * @plugin: A #FuPlugin
 * @component_id: An AppStream component id, e.g. "org.gnome.Software"
 * @version: A version string, e.g. "1.2.3"
 *
 * Sets a compile-time version of a specific dependancy.
 *
 * Since: 1.0.7
 **/
void
fu_plugin_add_compile_version (FuPlugin *plugin,
			       const gchar *component_id,
			       const gchar *version)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	if (priv->compile_versions == NULL)
		return;
	g_hash_table_insert (priv->compile_versions,
			     g_strdup (component_id),
			     g_strdup (version));
}

/**
 * fu_plugin_lookup_quirk_by_id:
 * @plugin: A #FuPlugin
 * @group: A string, e.g. "DfuFlags"
 * @key: An ID to match the entry, e.g. "Summary"
 *
 * Looks up an entry in the hardware database using a string value.
 *
 * Returns: (transfer none): values from the database, or %NULL if not found
 *
 * Since: 1.0.1
 **/
const gchar *
fu_plugin_lookup_quirk_by_id (FuPlugin *plugin, const gchar *group, const gchar *key)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	g_return_val_if_fail (FU_IS_PLUGIN (plugin), NULL);

	/* exact ID */
	return fu_quirks_lookup_by_id (priv->quirks, group, key);
}

/**
 * fu_plugin_lookup_quirk_by_id_as_uint64:
 * @plugin: A #FuPlugin
 * @group: A string, e.g. "DfuFlags"
 * @key: An ID to match the entry, e.g. "Size"
 *
 * Looks up an entry in the hardware database using a string key, returning
 * an integer value. Values are assumed base 10, unless prefixed with "0x"
 * where they are parsed as base 16.
 *
 * Returns: (transfer none): value from the database, or 0 if not found
 *
 * Since: 1.1.2
 **/
guint64
fu_plugin_lookup_quirk_by_id_as_uint64 (FuPlugin *plugin, const gchar *group, const gchar *key)
{
	return fu_common_strtoull (fu_plugin_lookup_quirk_by_id (plugin, group, key));
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

	/* no object loaded */
	if (priv->module == NULL)
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
		g_prefix_error (error, "failed to run %s() on %s: ",
				symbol_name + 10,
				priv->name);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_plugin_runner_flagged_device_generic (FuPlugin *plugin, FwupdInstallFlags flags,
					 FuDevice *device,
					 const gchar *symbol_name, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginFlaggedDeviceFunc func = NULL;

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
	if (!func (plugin, flags, device, error)) {
		g_prefix_error (error, "failed to run %s() on %s: ",
				symbol_name + 10,
				priv->name);
		return FALSE;
	}
	return TRUE;

}

static gboolean
fu_plugin_runner_device_array_generic (FuPlugin *plugin, GPtrArray *devices,
				       const gchar *symbol_name, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginDeviceArrayFunc func = NULL;

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
	if (!func (plugin, devices, error)) {
		g_prefix_error (error, "failed to run %s() on %s: ",
				symbol_name + 10,
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

	/* no object loaded */
	if (priv->module == NULL)
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
fu_plugin_runner_recoldplug (FuPlugin *plugin, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginStartupFunc func = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_recoldplug", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("performing recoldplug() on %s", priv->name);
	if (!func (plugin, error)) {
		g_prefix_error (error, "failed to recoldplug %s: ", priv->name);
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

	/* no object loaded */
	if (priv->module == NULL)
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

	/* no object loaded */
	if (priv->module == NULL)
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
fu_plugin_runner_composite_prepare (FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	return fu_plugin_runner_device_array_generic (plugin, devices,
						      "fu_plugin_composite_prepare",
						      error);
}

gboolean
fu_plugin_runner_composite_cleanup (FuPlugin *plugin, GPtrArray *devices, GError **error)
{
	return fu_plugin_runner_device_array_generic (plugin, devices,
						      "fu_plugin_composite_cleanup",
						      error);
}

gboolean
fu_plugin_runner_update_prepare (FuPlugin *plugin, FwupdInstallFlags flags, FuDevice *device,
				 GError **error)
{
	return fu_plugin_runner_flagged_device_generic (plugin, flags, device,
							"fu_plugin_update_prepare",
							error);
}

gboolean
fu_plugin_runner_update_cleanup (FuPlugin *plugin, FwupdInstallFlags flags, FuDevice *device,
				 GError **error)
{
	return fu_plugin_runner_flagged_device_generic (plugin, flags, device,
							"fu_plugin_update_cleanup",
							error);
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

/**
 * fu_plugin_add_udev_subsystem:
 * @plugin: a #FuPlugin
 * @subsystem: a subsystem name, e.g. `pciport`
 *
 * Registers the udev subsystem to be watched by the daemon.
 *
 * Plugins can use this method only in fu_plugin_init()
 **/
void
fu_plugin_add_udev_subsystem (FuPlugin *plugin, const gchar *subsystem)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	for (guint i = 0; i < priv->udev_subsystems->len; i++) {
		const gchar *subsystem_tmp = g_ptr_array_index (priv->udev_subsystems, i);
		if (g_strcmp0 (subsystem_tmp, subsystem) == 0)
			return;
	}
	g_debug ("added udev subsystem watch of %s", subsystem);
	g_ptr_array_add (priv->udev_subsystems, g_strdup (subsystem));
}

gboolean
fu_plugin_runner_usb_device_added (FuPlugin *plugin, GUsbDevice *usb_device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginUsbDeviceAddedFunc func = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* no object loaded */
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

gboolean
fu_plugin_runner_udev_device_added (FuPlugin *plugin, GUdevDevice *udev_device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (plugin);
	FuPluginUdevDeviceAddedFunc func = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_udev_device_added", (gpointer *) &func);
	if (func != NULL) {
		g_debug ("performing udev_device_added() on %s", priv->name);
		return func (plugin, udev_device, error);
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

	/* don't notify plugins on their own devices */
	if (g_strcmp0 (fu_device_get_plugin (device), fu_plugin_get_name (plugin)) == 0)
		return;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_device_registered", (gpointer *) &func);
	if (func != NULL) {
		g_debug ("performing fu_plugin_device_registered() on %s", priv->name);
		func (plugin, device);
	}
}

static gboolean
fu_plugin_runner_schedule_update (FuPlugin *plugin,
			     FuDevice *device,
			     GBytes *blob_cab,
			     GError **error)
{
	FwupdRelease *release;
	gchar tmpname[] = {"XXXXXX.cap"};
	g_autofree gchar *dirname = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuDevice) res_tmp = NULL;
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(FwupdRelease) release_tmp = fwupd_release_new ();
	g_autoptr(GFile) file = NULL;

	/* id already exists */
	history = fu_history_new ();
	res_tmp = fu_history_get_device_by_id (history, fu_device_get_id (device), NULL);
	if (res_tmp != NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_ALREADY_PENDING,
			     "%s is already scheduled to be updated",
			     fu_device_get_id (device));
		return FALSE;
	}

	/* create directory */
	dirname = fu_common_get_path (FU_PATH_KIND_LOCALSTATEDIR_PKG);
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
	fu_device_set_status (device, FWUPD_STATUS_SCHEDULING);
	if (!g_file_set_contents (filename,
				  g_bytes_get_data (blob_cab, NULL),
				  (gssize) g_bytes_get_size (blob_cab),
				  error))
		return FALSE;

	/* schedule for next boot */
	g_debug ("schedule %s to be installed to %s on next boot",
		 filename, fu_device_get_id (device));
	release = fu_device_get_release_default (device);
	fwupd_release_set_version (release_tmp, fwupd_release_get_version (release));
	fwupd_release_set_filename (release_tmp, filename);

	/* add to database */
	fu_device_set_update_state (device, FWUPD_UPDATE_STATE_PENDING);
	if (!fu_history_add_device (history, device, release_tmp, error))
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

	/* no object loaded */
	if (priv->module == NULL)
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
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(FuDevice) device_pending = NULL;
	GError *error_update = NULL;
	GPtrArray *checksums;

	/* not enabled */
	if (!priv->enabled) {
		g_debug ("plugin not enabled, skipping");
		return TRUE;
	}

	/* no object loaded */
	if (priv->module == NULL) {
		g_debug ("module not enabled, skipping");
		return TRUE;
	}

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
		if (blob_cab == NULL) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_NOT_SUPPORTED,
					     "No cabinet archive to schedule");
			return FALSE;
		}
		return fu_plugin_runner_schedule_update (plugin,
							 device,
							 blob_cab,
							 error);
	}

	/* cancel the pending action */
	if (!fu_plugin_runner_offline_invalidate (error))
		return FALSE;

	/* online */
	history = fu_history_new ();
	device_pending = fu_history_get_device_by_id (history, fu_device_get_id (device), NULL);
	if (!update_func (plugin, device, blob_fw, flags, &error_update)) {
		fu_device_set_update_error (device, error_update->message);
		g_propagate_error (error, error_update);
		return FALSE;
	}

	/* no longer valid */
	checksums = fu_device_get_checksums (device);
	g_ptr_array_set_size (checksums, 0);

	/* cleanup */
	if (device_pending != NULL) {
		const gchar *tmp;
		FwupdRelease *release;

		/* update history database */
		fu_device_set_update_state (device, FWUPD_UPDATE_STATE_SUCCESS);
		if (!fu_history_modify_device (history, device,
					       FU_HISTORY_FLAGS_MATCH_NEW_VERSION,
					       error))
			return FALSE;

		/* delete cab file */
		release = fu_device_get_release_default (device_pending);
		tmp = fwupd_release_get_filename (release);
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

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_get_results", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("performing clear_result() on %s", priv->name);
	if (!func (plugin, device, error)) {
		g_prefix_error (error, "failed to clear_result %s: ", priv->name);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_runner_get_results (FuPlugin *plugin, FuDevice *device, GError **error)
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
	g_module_symbol (priv->module, "fu_plugin_get_results", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("performing get_results() on %s", priv->name);
	if (!func (plugin, device, error)) {
		g_prefix_error (error, "failed to get_results %s: ", priv->name);
		return FALSE;
	}
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
 * fu_plugin_get_priority:
 * @plugin: a #FuPlugin
 *
 * Gets the plugin priority, where higher numbers are better.
 *
 * Returns: the integer value
 **/
guint
fu_plugin_get_priority (FuPlugin *plugin)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (plugin);
	return priv->priority;
}

/**
 * fu_plugin_set_priority:
 * @plugin: a #FuPlugin
 * @priority: a integer value
 *
 * Sets the plugin priority, where higher numbers are better.
 **/
void
fu_plugin_set_priority (FuPlugin *plugin, guint priority)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (plugin);
	priv->priority = priority;
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

/**
 * fu_plugin_has_rule:
 * @plugin: a #FuPlugin
 * @rule: a #FuPluginRule, e.g. %FU_PLUGIN_RULE_CONFLICTS
 * @name: a plugin name, e.g. `upower`
 *
 * Gets the plugin IDs that should be run after this plugin.
 *
 * Returns: %TRUE if the name exists for the specific rule
 **/
gboolean
fu_plugin_has_rule (FuPlugin *plugin, FuPluginRule rule, const gchar *name)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (plugin);
	for (guint i = 0; i < priv->rules[rule]->len; i++) {
		const gchar *tmp = g_ptr_array_index (priv->rules[rule], i);
		if (g_strcmp0 (tmp, name) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fu_plugin_add_report_metadata:
 * @plugin: a #FuPlugin
 * @key: a string, e.g. `FwupdateVersion`
 * @value: a string, e.g. `10`
 *
 * Sets any additional metadata to be included in the firmware report to aid
 * debugging problems.
 *
 * Any data included here will be sent to the metadata server after user
 * confirmation.
 **/
void
fu_plugin_add_report_metadata (FuPlugin *plugin, const gchar *key, const gchar *value)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (plugin);
	g_hash_table_insert (priv->report_metadata, g_strdup (key), g_strdup (value));
}

/**
 * fu_plugin_get_report_metadata:
 * @plugin: a #FuPlugin
 *
 * Returns the list of additional metadata to be added when filing a report.
 *
 * Returns: (transfer none): the map of report metadata
 **/
GHashTable *
fu_plugin_get_report_metadata (FuPlugin *plugin)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (plugin);
	return priv->report_metadata;
}

/**
 * fu_plugin_get_config_value:
 * @plugin: a #FuPlugin
 * @key: A settings key
 *
 * Return the value of a key if it's been configured
 *
 * Since: 1.0.6
 **/
gchar *
fu_plugin_get_config_value (FuPlugin *plugin, const gchar *key)
{
	g_autofree gchar *conf_dir = NULL;
	g_autofree gchar *conf_file = NULL;
	g_autofree gchar *conf_path = NULL;
	g_autoptr(GKeyFile) keyfile = NULL;
	const gchar *plugin_name;

	conf_dir = fu_common_get_path (FU_PATH_KIND_SYSCONFDIR_PKG);
	plugin_name = fu_plugin_get_name (plugin);
	conf_file = g_strdup_printf ("%s.conf", plugin_name);
	conf_path = g_build_filename (conf_dir, conf_file,  NULL);
	if (!g_file_test (conf_path, G_FILE_TEST_IS_REGULAR))
		return NULL;
	keyfile = g_key_file_new ();
	if (!g_key_file_load_from_file (keyfile, conf_path,
					G_KEY_FILE_NONE, NULL))
		return NULL;
	return g_key_file_get_string (keyfile, plugin_name, key, NULL);
}

/**
 * fu_plugin_name_compare:
 * @plugin1: first #FuPlugin to compare.
 * @plugin2: second #FuPlugin to compare.
 *
 * Compares two plugins by their names.
 *
 * Returns: 1, 0 or -1 if @plugin1 is greater, equal, or less than @plugin2.
 **/
gint
fu_plugin_name_compare (FuPlugin *plugin1, FuPlugin *plugin2)
{
	FuPluginPrivate *priv1 = fu_plugin_get_instance_private (plugin1);
	FuPluginPrivate *priv2 = fu_plugin_get_instance_private (plugin2);
	return g_strcmp0 (priv1->name, priv2->name);
}

/**
 * fu_plugin_order_compare:
 * @plugin1: first #FuPlugin to compare.
 * @plugin2: second #FuPlugin to compare.
 *
 * Compares two plugins by their depsolved order.
 *
 * Returns: 1, 0 or -1 if @plugin1 is greater, equal, or less than @plugin2.
 **/
gint
fu_plugin_order_compare (FuPlugin *plugin1, FuPlugin *plugin2)
{
	FuPluginPrivate *priv1 = fu_plugin_get_instance_private (plugin1);
	FuPluginPrivate *priv2 = fu_plugin_get_instance_private (plugin2);
	if (priv1->order < priv2->order)
		return -1;
	if (priv1->order > priv2->order)
		return 1;
	return 0;
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
	priv->report_metadata = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
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
	if (priv->udev_subsystems != NULL)
		g_ptr_array_unref (priv->udev_subsystems);
	if (priv->smbios != NULL)
		g_object_unref (priv->smbios);
	if (priv->runtime_versions != NULL)
		g_hash_table_unref (priv->runtime_versions);
	if (priv->compile_versions != NULL)
		g_hash_table_unref (priv->compile_versions);
	g_hash_table_unref (priv->devices);
	g_hash_table_unref (priv->devices_delay);
	g_hash_table_unref (priv->report_metadata);
	g_free (priv->name);
	g_free (priv->data);
	/* Must happen as the last step to avoid prematurely
	 * freeing memory held by the plugin */
#ifndef RUNNING_ON_VALGRIND
	if (priv->module != NULL)
		g_module_close (priv->module);
#endif

	G_OBJECT_CLASS (fu_plugin_parent_class)->finalize (object);
}

FuPlugin *
fu_plugin_new (void)
{
	FuPlugin *plugin;
	plugin = g_object_new (FU_TYPE_PLUGIN, NULL);
	return plugin;
}

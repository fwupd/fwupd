/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuPlugin"

#include "config.h"

#include <fwupd.h>
#include <gmodule.h>
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
#include "fu-mutex.h"

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
	gchar			*build_hash;
	FuHwids			*hwids;
	FuQuirks		*quirks;
	GHashTable		*runtime_versions;
	GHashTable		*compile_versions;
	GPtrArray		*udev_subsystems;
	FuSmbios		*smbios;
	GHashTable		*devices;	/* platform_id:GObject */
	GRWLock			 devices_mutex;
	GHashTable		*report_metadata;	/* key:value */
	FuPluginData		*data;
} FuPluginPrivate;

enum {
	SIGNAL_DEVICE_ADDED,
	SIGNAL_DEVICE_REMOVED,
	SIGNAL_DEVICE_REGISTER,
	SIGNAL_RULES_CHANGED,
	SIGNAL_RECOLDPLUG,
	SIGNAL_SET_COLDPLUG_DELAY,
	SIGNAL_CHECK_SUPPORTED,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (FuPlugin, fu_plugin, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_plugin_get_instance_private (o))

typedef const gchar	*(*FuPluginGetNameFunc)		(void);
typedef void		 (*FuPluginInitFunc)		(FuPlugin	*self);
typedef gboolean	 (*FuPluginStartupFunc)		(FuPlugin	*self,
							 GError		**error);
typedef void		 (*FuPluginDeviceRegisterFunc)	(FuPlugin	*self,
							 FuDevice	*device);
typedef gboolean	 (*FuPluginDeviceFunc)		(FuPlugin	*self,
							 FuDevice	*device,
							 GError		**error);
typedef gboolean	 (*FuPluginFlaggedDeviceFunc)	(FuPlugin	*self,
							 FwupdInstallFlags flags,
							 FuDevice	*device,
							 GError		**error);
typedef gboolean	 (*FuPluginDeviceArrayFunc)	(FuPlugin	*self,
							 GPtrArray	*devices,
							 GError		**error);
typedef gboolean	 (*FuPluginVerifyFunc)		(FuPlugin	*self,
							 FuDevice	*device,
							 FuPluginVerifyFlags flags,
							 GError		**error);
typedef gboolean	 (*FuPluginUpdateFunc)		(FuPlugin	*self,
							 FuDevice	*device,
							 GBytes		*blob_fw,
							 FwupdInstallFlags flags,
							 GError		**error);
typedef gboolean	 (*FuPluginUsbDeviceAddedFunc)	(FuPlugin	*self,
							 FuUsbDevice	*device,
							 GError		**error);
typedef gboolean	 (*FuPluginUdevDeviceAddedFunc)	(FuPlugin	*self,
							 FuUdevDevice	*device,
							 GError		**error);

/**
 * fu_plugin_get_name:
 * @self: A #FuPlugin
 *
 * Gets the plugin name.
 *
 * Returns: a plugin name, or %NULL for unknown.
 *
 * Since: 0.8.0
 **/
const gchar *
fu_plugin_get_name (FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_PLUGIN (self), NULL);
	return priv->name;
}

void
fu_plugin_set_name (FuPlugin *self, const gchar *name)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_PLUGIN (self));
	g_return_if_fail (name != NULL);
	g_free (priv->name);
	priv->name = g_strdup (name);
}

/**
 * fu_plugin_set_build_hash:
 * @self: A #FuPlugin
 * @build_hash: A checksum
 *
 * Sets the plugin build hash, typically a SHA256 checksum. All plugins must
 * set the correct checksum to avoid the daemon being marked as tainted.
 *
 * Since: 1.2.4
 **/
void
fu_plugin_set_build_hash (FuPlugin *self, const gchar *build_hash)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_PLUGIN (self));
	g_return_if_fail (build_hash != NULL);
	g_free (priv->build_hash);
	priv->build_hash = g_strdup (build_hash);
}

const gchar *
fu_plugin_get_build_hash (FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_PLUGIN (self), NULL);
	return priv->build_hash;
}

/**
 * fu_plugin_cache_lookup:
 * @self: A #FuPlugin
 * @id: the key
 *
 * Finds an object in the per-plugin cache.
 *
 * Returns: (transfer none): a #GObject, or %NULL for unfound.
 *
 * Since: 0.8.0
 **/
gpointer
fu_plugin_cache_lookup (FuPlugin *self, const gchar *id)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GRWLockReaderLocker) locker = g_rw_lock_reader_locker_new (&priv->devices_mutex);
	g_return_val_if_fail (FU_IS_PLUGIN (self), NULL);
	g_return_val_if_fail (id != NULL, NULL);
	g_return_val_if_fail (locker != NULL, NULL);
	return g_hash_table_lookup (priv->devices, id);
}

/**
 * fu_plugin_cache_add:
 * @self: A #FuPlugin
 * @id: the key
 * @dev: a #GObject, typically a #FuDevice
 *
 * Adds an object to the per-plugin cache.
 *
 * Since: 0.8.0
 **/
void
fu_plugin_cache_add (FuPlugin *self, const gchar *id, gpointer dev)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GRWLockReaderLocker) locker = g_rw_lock_writer_locker_new (&priv->devices_mutex);
	g_return_if_fail (FU_IS_PLUGIN (self));
	g_return_if_fail (id != NULL);
	g_return_if_fail (locker != NULL);
	g_hash_table_insert (priv->devices, g_strdup (id), g_object_ref (dev));
}

/**
 * fu_plugin_cache_remove:
 * @self: A #FuPlugin
 * @id: the key
 *
 * Removes an object from the per-plugin cache.
 *
 * Since: 0.8.0
 **/
void
fu_plugin_cache_remove (FuPlugin *self, const gchar *id)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_autoptr(GRWLockReaderLocker) locker = g_rw_lock_writer_locker_new (&priv->devices_mutex);
	g_return_if_fail (FU_IS_PLUGIN (self));
	g_return_if_fail (id != NULL);
	g_return_if_fail (locker != NULL);
	g_hash_table_remove (priv->devices, id);
}

/**
 * fu_plugin_get_data:
 * @self: A #FuPlugin
 *
 * Gets the per-plugin allocated private data. This will return %NULL unless
 * fu_plugin_alloc_data() has been called by the plugin.
 *
 * Returns: (transfer none): a pointer to a structure, or %NULL for unset.
 *
 * Since: 0.8.0
 **/
FuPluginData *
fu_plugin_get_data (FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_PLUGIN (self), NULL);
	return priv->data;
}

/**
 * fu_plugin_alloc_data:
 * @self: A #FuPlugin
 * @data_sz: the size to allocate
 *
 * Allocates the per-plugin allocated private data.
 *
 * Returns: (transfer full): a pointer to a structure, or %NULL for unset.
 *
 * Since: 0.8.0
 **/
FuPluginData *
fu_plugin_alloc_data (FuPlugin *self, gsize data_sz)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_PLUGIN (self), NULL);
	if (priv->data != NULL) {
		g_critical ("fu_plugin_alloc_data() already used by plugin");
		return priv->data;
	}
	priv->data = g_malloc0 (data_sz);
	return priv->data;
}

/**
 * fu_plugin_get_usb_context:
 * @self: A #FuPlugin
 *
 * Gets the shared USB context that all plugins can use.
 *
 * Returns: (transfer none): a #GUsbContext.
 *
 * Since: 0.8.0
 **/
GUsbContext *
fu_plugin_get_usb_context (FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_PLUGIN (self), NULL);
	return priv->usb_ctx;
}

void
fu_plugin_set_usb_context (FuPlugin *self, GUsbContext *usb_ctx)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_set_object (&priv->usb_ctx, usb_ctx);
}

/**
 * fu_plugin_get_enabled:
 * @self: A #FuPlugin
 *
 * Returns if the plugin is enabled. Plugins may self-disable using
 * fu_plugin_set_enabled() or can be disabled by the daemon.
 *
 * Returns: %TRUE if the plugin is currently enabled.
 *
 * Since: 0.8.0
 **/
gboolean
fu_plugin_get_enabled (FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_PLUGIN (self), FALSE);
	return priv->enabled;
}

/**
 * fu_plugin_set_enabled:
 * @self: A #FuPlugin
 * @enabled: the enabled value
 *
 * Enables or disables a plugin. Plugins can self-disable at any point.
 *
 * Since: 0.8.0
 **/
void
fu_plugin_set_enabled (FuPlugin *self, gboolean enabled)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_return_if_fail (FU_IS_PLUGIN (self));
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
fu_plugin_open (FuPlugin *self, const gchar *filename, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
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
		func (self);
	}

	return TRUE;
}

/**
 * fu_plugin_device_add:
 * @self: A #FuPlugin
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
fu_plugin_device_add (FuPlugin *self, FuDevice *device)
{
	GPtrArray *children;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (FU_IS_PLUGIN (self));
	g_return_if_fail (FU_IS_DEVICE (device));

	/* ensure the device ID is set from the physical and logical IDs */
	if (!fu_device_ensure_id (device, &error)) {
		g_warning ("ignoring add: %s", error->message);
		return;
	}

	g_debug ("emit added from %s: %s",
		 fu_plugin_get_name (self),
		 fu_device_get_id (device));
	fu_device_set_created (device, (guint64) g_get_real_time () / G_USEC_PER_SEC);
	fu_device_set_plugin (device, fu_plugin_get_name (self));
	g_signal_emit (self, signals[SIGNAL_DEVICE_ADDED], 0, device);

	/* add children if they have not already been added */
	children = fu_device_get_children (device);
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index (children, i);
		if (fu_device_get_created (child) == 0)
			fu_plugin_device_add (self, child);
	}
}

/**
 * fu_plugin_device_register:
 * @self: A #FuPlugin
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
fu_plugin_device_register (FuPlugin *self, FuDevice *device)
{
	g_autoptr(GError) error = NULL;

	g_return_if_fail (FU_IS_PLUGIN (self));
	g_return_if_fail (FU_IS_DEVICE (device));

	/* ensure the device ID is set from the physical and logical IDs */
	if (!fu_device_ensure_id (device, &error)) {
		g_warning ("ignoring registration: %s", error->message);
		return;
	}

	g_debug ("emit device-register from %s: %s",
		 fu_plugin_get_name (self),
		 fu_device_get_id (device));
	g_signal_emit (self, signals[SIGNAL_DEVICE_REGISTER], 0, device);
}

/**
 * fu_plugin_device_remove:
 * @self: A #FuPlugin
 * @device: A #FuDevice
 *
 * Asks the daemon to remove a device from the exported list.
 *
 * Since: 0.8.0
 **/
void
fu_plugin_device_remove (FuPlugin *self, FuDevice *device)
{
	g_return_if_fail (FU_IS_PLUGIN (self));
	g_return_if_fail (FU_IS_DEVICE (device));

	g_debug ("emit removed from %s: %s",
		 fu_plugin_get_name (self),
		 fu_device_get_id (device));
	g_signal_emit (self, signals[SIGNAL_DEVICE_REMOVED], 0, device);
}

/**
 * fu_plugin_request_recoldplug:
 * @self: A #FuPlugin
 *
 * Ask all the plugins to coldplug all devices, which will include the prepare()
 * and cleanup() phases. Duplicate devices added will be ignored.
 *
 * Since: 0.8.0
 **/
void
fu_plugin_request_recoldplug (FuPlugin *self)
{
	g_return_if_fail (FU_IS_PLUGIN (self));
	g_signal_emit (self, signals[SIGNAL_RECOLDPLUG], 0);
}

/**
 * fu_plugin_check_hwid:
 * @self: A #FuPlugin
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
fu_plugin_check_hwid (FuPlugin *self, const gchar *hwid)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	if (priv->hwids == NULL)
		return FALSE;
	return fu_hwids_has_guid (priv->hwids, hwid);
}

/**
 * fu_plugin_get_hwids:
 * @self: A #FuPlugin
 *
 * Returns all the HWIDs defined in the system. All hardware IDs on a
 * specific system can be shown using the `fwupdmgr hwids` command.
 *
 * Returns: (transfer none) (element-type utf-8): An array of GUIDs
 *
 * Since: 1.1.1
 **/
GPtrArray *
fu_plugin_get_hwids (FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	if (priv->hwids == NULL)
		return NULL;
	return fu_hwids_get_guids (priv->hwids);
}

/**
 * fu_plugin_check_supported:
 * @self: A #FuPlugin
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
fu_plugin_check_supported (FuPlugin *self, const gchar *guid)
{
	gboolean retval = FALSE;
	g_signal_emit (self, signals[SIGNAL_CHECK_SUPPORTED], 0, guid, &retval);
	return retval;
}

/**
 * fu_plugin_get_dmi_value:
 * @self: A #FuPlugin
 * @dmi_id: A DMI ID, e.g. `BiosVersion`
 *
 * Gets a hardware DMI value.
 *
 * Returns: The string, or %NULL
 *
 * Since: 0.9.7
 **/
const gchar *
fu_plugin_get_dmi_value (FuPlugin *self, const gchar *dmi_id)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	if (priv->hwids == NULL)
		return NULL;
	return fu_hwids_get_value (priv->hwids, dmi_id);
}

/**
 * fu_plugin_get_smbios_string:
 * @self: A #FuPlugin
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
fu_plugin_get_smbios_string (FuPlugin *self, guint8 structure_type, guint8 offset)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	if (priv->smbios == NULL)
		return NULL;
	return fu_smbios_get_string (priv->smbios, structure_type, offset, NULL);
}

/**
 * fu_plugin_get_smbios_data:
 * @self: A #FuPlugin
 * @structure_type: A SMBIOS structure type, e.g. %FU_SMBIOS_STRUCTURE_TYPE_BIOS
 *
 * Gets a hardware SMBIOS data.
 *
 * Returns: (transfer full): A #GBytes, or %NULL
 *
 * Since: 0.9.8
 **/
GBytes *
fu_plugin_get_smbios_data (FuPlugin *self, guint8 structure_type)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	if (priv->smbios == NULL)
		return NULL;
	return fu_smbios_get_data (priv->smbios, structure_type, NULL);
}

void
fu_plugin_set_hwids (FuPlugin *self, FuHwids *hwids)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_set_object (&priv->hwids, hwids);
}

void
fu_plugin_set_udev_subsystems (FuPlugin *self, GPtrArray *udev_subsystems)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	if (priv->udev_subsystems != NULL)
		g_ptr_array_unref (priv->udev_subsystems);
	priv->udev_subsystems = g_ptr_array_ref (udev_subsystems);
}

void
fu_plugin_set_quirks (FuPlugin *self, FuQuirks *quirks)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_set_object (&priv->quirks, quirks);
}

/**
 * fu_plugin_get_quirks:
 * @self: A #FuPlugin
 *
 * Returns the hardware database object. This can be used to discover device
 * quirks or other device-specific settings.
 *
 * Returns: (transfer none): a #FuQuirks, or %NULL if not set
 *
 * Since: 1.0.1
 **/
FuQuirks *
fu_plugin_get_quirks (FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	return priv->quirks;
}

void
fu_plugin_set_runtime_versions (FuPlugin *self, GHashTable *runtime_versions)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	priv->runtime_versions = g_hash_table_ref (runtime_versions);
}

/**
 * fu_plugin_add_runtime_version:
 * @self: A #FuPlugin
 * @component_id: An AppStream component id, e.g. "org.gnome.Software"
 * @version: A version string, e.g. "1.2.3"
 *
 * Sets a runtime version of a specific dependency.
 *
 * Since: 1.0.7
 **/
void
fu_plugin_add_runtime_version (FuPlugin *self,
			       const gchar *component_id,
			       const gchar *version)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	if (priv->runtime_versions == NULL)
		return;
	g_hash_table_insert (priv->runtime_versions,
			     g_strdup (component_id),
			     g_strdup (version));
}

void
fu_plugin_set_compile_versions (FuPlugin *self, GHashTable *compile_versions)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	priv->compile_versions = g_hash_table_ref (compile_versions);
}

/**
 * fu_plugin_add_compile_version:
 * @self: A #FuPlugin
 * @component_id: An AppStream component id, e.g. "org.gnome.Software"
 * @version: A version string, e.g. "1.2.3"
 *
 * Sets a compile-time version of a specific dependency.
 *
 * Since: 1.0.7
 **/
void
fu_plugin_add_compile_version (FuPlugin *self,
			       const gchar *component_id,
			       const gchar *version)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	if (priv->compile_versions == NULL)
		return;
	g_hash_table_insert (priv->compile_versions,
			     g_strdup (component_id),
			     g_strdup (version));
}

/**
 * fu_plugin_lookup_quirk_by_id:
 * @self: A #FuPlugin
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
fu_plugin_lookup_quirk_by_id (FuPlugin *self, const gchar *group, const gchar *key)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_PLUGIN (self), NULL);

	/* exact ID */
	return fu_quirks_lookup_by_id (priv->quirks, group, key);
}

/**
 * fu_plugin_lookup_quirk_by_id_as_uint64:
 * @self: A #FuPlugin
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
fu_plugin_lookup_quirk_by_id_as_uint64 (FuPlugin *self, const gchar *group, const gchar *key)
{
	return fu_common_strtoull (fu_plugin_lookup_quirk_by_id (self, group, key));
}

void
fu_plugin_set_smbios (FuPlugin *self, FuSmbios *smbios)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_set_object (&priv->smbios, smbios);
}

/**
 * fu_plugin_set_coldplug_delay:
 * @self: A #FuPlugin
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
fu_plugin_set_coldplug_delay (FuPlugin *self, guint duration)
{
	g_return_if_fail (FU_IS_PLUGIN (self));
	g_return_if_fail (duration > 0);

	/* check sanity */
	if (duration > FU_PLUGIN_COLDPLUG_DELAY_MAXIMUM) {
		g_warning ("duration of %ums is crazy, truncating to %ums",
			   duration,
			   FU_PLUGIN_COLDPLUG_DELAY_MAXIMUM);
		duration = FU_PLUGIN_COLDPLUG_DELAY_MAXIMUM;
	}

	/* emit */
	g_signal_emit (self, signals[SIGNAL_SET_COLDPLUG_DELAY], 0, duration);
}

gboolean
fu_plugin_runner_startup (FuPlugin *self, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginStartupFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

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
	if (!func (self, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset error in plugin %s for startup()",
				    priv->name);
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to startup using %s: ",
					    priv->name);
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
	g_autofree gchar *filename = NULL;
	g_autofree gchar *symlink_target = fu_common_get_path (FU_PATH_KIND_LOCALSTATEDIR_PKG);

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* does already exist */
	filename = fu_common_realpath (FU_OFFLINE_TRIGGER_FILENAME, NULL);
	if (g_strcmp0 (filename, symlink_target) == 0) {
		g_debug ("%s already points to %s, skipping creation",
			 FU_OFFLINE_TRIGGER_FILENAME, symlink_target);
		return TRUE;
	}

	/* create symlink for the systemd-system-update-generator */
	rc = symlink (symlink_target, FU_OFFLINE_TRIGGER_FILENAME);
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
fu_plugin_runner_device_generic (FuPlugin *self, FuDevice *device,
				 const gchar *symbol_name, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginDeviceFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

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
	if (!func (self, device, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset error in plugin %s for %s()",
				    priv->name, symbol_name + 10);
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to %s using %s: ",
					    symbol_name + 10, priv->name);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_plugin_runner_flagged_device_generic (FuPlugin *self, FwupdInstallFlags flags,
					 FuDevice *device,
					 const gchar *symbol_name, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginFlaggedDeviceFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

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
	if (!func (self, flags, device, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset error in plugin %s for %s()",
				    priv->name, symbol_name + 10);
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to %s using %s: ",
					    symbol_name + 10, priv->name);
		return FALSE;
	}
	return TRUE;

}

static gboolean
fu_plugin_runner_device_array_generic (FuPlugin *self, GPtrArray *devices,
				       const gchar *symbol_name, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginDeviceArrayFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

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
	if (!func (self, devices, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset error in plugin %s for %s()",
				    priv->name, symbol_name + 10);
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to %s using %s: ",
					    symbol_name + 10, priv->name);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_runner_coldplug (FuPlugin *self, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginStartupFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

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
	if (!func (self, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset error in plugin %s for coldplug()",
				    priv->name);
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to coldplug using %s: ", priv->name);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_runner_recoldplug (FuPlugin *self, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginStartupFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

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
	if (!func (self, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset error in plugin %s for recoldplug()",
				    priv->name);
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to recoldplug using %s: ",
					    priv->name);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_runner_coldplug_prepare (FuPlugin *self, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginStartupFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

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
	if (!func (self, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset error in plugin %s for coldplug_prepare()",
				    priv->name);
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to coldplug_prepare using %s: ",
					    priv->name);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_runner_coldplug_cleanup (FuPlugin *self, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginStartupFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

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
	if (!func (self, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset error in plugin %s for coldplug_cleanup()",
				    priv->name);
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to coldplug_cleanup using %s: ",
					    priv->name);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_runner_composite_prepare (FuPlugin *self, GPtrArray *devices, GError **error)
{
	return fu_plugin_runner_device_array_generic (self, devices,
						      "fu_plugin_composite_prepare",
						      error);
}

gboolean
fu_plugin_runner_composite_cleanup (FuPlugin *self, GPtrArray *devices, GError **error)
{
	return fu_plugin_runner_device_array_generic (self, devices,
						      "fu_plugin_composite_cleanup",
						      error);
}

gboolean
fu_plugin_runner_update_prepare (FuPlugin *self, FwupdInstallFlags flags, FuDevice *device,
				 GError **error)
{
	return fu_plugin_runner_flagged_device_generic (self, flags, device,
							"fu_plugin_update_prepare",
							error);
}

gboolean
fu_plugin_runner_update_cleanup (FuPlugin *self, FwupdInstallFlags flags, FuDevice *device,
				 GError **error)
{
	return fu_plugin_runner_flagged_device_generic (self, flags, device,
							"fu_plugin_update_cleanup",
							error);
}

gboolean
fu_plugin_runner_update_attach (FuPlugin *self, FuDevice *device, GError **error)
{
	return fu_plugin_runner_device_generic (self, device,
						"fu_plugin_update_attach", error);
}

gboolean
fu_plugin_runner_update_detach (FuPlugin *self, FuDevice *device, GError **error)
{
	return fu_plugin_runner_device_generic (self, device,
						"fu_plugin_update_detach", error);
}

gboolean
fu_plugin_runner_update_reload (FuPlugin *self, FuDevice *device, GError **error)
{
	return fu_plugin_runner_device_generic (self, device,
						"fu_plugin_update_reload", error);
}

/**
 * fu_plugin_add_udev_subsystem:
 * @self: a #FuPlugin
 * @subsystem: a subsystem name, e.g. `pciport`
 *
 * Registers the udev subsystem to be watched by the daemon.
 *
 * Plugins can use this method only in fu_plugin_init()
 **/
void
fu_plugin_add_udev_subsystem (FuPlugin *self, const gchar *subsystem)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	for (guint i = 0; i < priv->udev_subsystems->len; i++) {
		const gchar *subsystem_tmp = g_ptr_array_index (priv->udev_subsystems, i);
		if (g_strcmp0 (subsystem_tmp, subsystem) == 0)
			return;
	}
	g_debug ("added udev subsystem watch of %s", subsystem);
	g_ptr_array_add (priv->udev_subsystems, g_strdup (subsystem));
}

gboolean
fu_plugin_runner_usb_device_added (FuPlugin *self, FuUsbDevice *device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginUsbDeviceAddedFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_usb_device_added", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("performing usb_device_added() on %s", priv->name);
	if (!func (self, device, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset error in plugin %s for usb_device_added()",
				    priv->name);
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to add device using on %s: ",
					    priv->name);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_runner_udev_device_added (FuPlugin *self, FuUdevDevice *device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginUdevDeviceAddedFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_udev_device_added", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("performing udev_device_added() on %s", priv->name);
	if (!func (self, device, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset error in plugin %s for udev_device_added()",
				    priv->name);
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to add device using on %s: ",
					    priv->name);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_runner_udev_device_changed (FuPlugin *self, FuUdevDevice *device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginUdevDeviceAddedFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_udev_device_changed", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("performing udev_device_changed() on %s", priv->name);
	if (!func (self, device, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset error in plugin %s for udev_device_changed()",
				    priv->name);
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to change device on %s: ",
					    priv->name);
		return FALSE;
	}
	return TRUE;
}

void
fu_plugin_runner_device_removed (FuPlugin *self, FuDevice *device)
{
	g_autoptr(GError) error_local= NULL;

	if (!fu_plugin_runner_device_generic (self, device,
					      "fu_plugin_device_removed",
					      &error_local))
		g_warning ("%s", error_local->message);
}

void
fu_plugin_runner_device_register (FuPlugin *self, FuDevice *device)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginDeviceRegisterFunc func = NULL;

	/* not enabled */
	if (!priv->enabled)
		return;
	if (priv->module == NULL)
		return;

	/* don't notify plugins on their own devices */
	if (g_strcmp0 (fu_device_get_plugin (device), fu_plugin_get_name (self)) == 0)
		return;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_device_registered", (gpointer *) &func);
	if (func != NULL) {
		g_debug ("performing fu_plugin_device_registered() on %s", priv->name);
		func (self, device);
	}
}

gboolean
fu_plugin_runner_schedule_update (FuPlugin *self,
			     FuDevice *device,
			     FwupdRelease *release,
			     GBytes *blob_cab,
			     FwupdInstallFlags flags,
			     GError **error)
{
	gchar tmpname[] = {"XXXXXX.cab"};
	g_autofree gchar *dirname = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(GFile) file = NULL;

	/* id already exists */
	history = fu_history_new ();
	if ((flags & FWUPD_INSTALL_FLAG_FORCE) == 0) {
		g_autoptr(FuDevice) res_tmp = NULL;
		res_tmp = fu_history_get_device_by_id (history, fu_device_get_id (device), NULL);
		if (res_tmp != NULL &&
		    fu_device_get_update_state (res_tmp) == FWUPD_UPDATE_STATE_PENDING) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_ALREADY_PENDING,
				     "%s is already scheduled to be updated",
				     fu_device_get_id (device));
			return FALSE;
		}
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
	fwupd_release_set_filename (release, filename);

	/* add to database */
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_set_update_state (device, FWUPD_UPDATE_STATE_PENDING);
	if (!fu_history_add_device (history, device, release, error))
		return FALSE;

	/* next boot we run offline */
	fu_device_set_progress (device, 100);
	return fu_plugin_runner_offline_setup (error);
}

gboolean
fu_plugin_runner_verify (FuPlugin *self,
			 FuDevice *device,
			 FuPluginVerifyFlags flags,
			 GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginVerifyFunc func = NULL;
	GPtrArray *checksums;
	g_autoptr(GError) error_local = NULL;

	/* not enabled */
	if (!priv->enabled)
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_verify", (gpointer *) &func);
	if (func == NULL)
		return TRUE;

	/* clear any existing verification checksums */
	checksums = fu_device_get_checksums (device);
	g_ptr_array_set_size (checksums, 0);

	/* run additional detach */
	if (!fu_plugin_runner_device_generic (self, device,
					      "fu_plugin_verify_detach",
					      error))
		return FALSE;

	/* run vfunc */
	g_debug ("performing verify() on %s", priv->name);
	if (!func (self, device, flags, &error_local)) {
		g_autoptr(GError) error_attach = NULL;
		if (error_local == NULL) {
			g_critical ("unset error in plugin %s for verify()",
				    priv->name);
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to verify using %s: ",
					    priv->name);
		/* make the device "work" again, but don't prefix the error */
		if (!fu_plugin_runner_device_generic (self, device,
						      "fu_plugin_verify_attach",
						      &error_attach)) {
			g_warning ("failed to attach whilst aborting verify(): %s",
				   error_attach->message);
		}
		return FALSE;
	}

	/* run optional attach */
	if (!fu_plugin_runner_device_generic (self, device,
					      "fu_plugin_verify_attach",
					      error))
		return FALSE;

	/* success */
	return TRUE;
}

gboolean
fu_plugin_runner_activate (FuPlugin *self, FuDevice *device, GError **error)
{
	guint64 flags;

	/* final check */
	flags = fu_device_get_flags (device);
	if ((flags & FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION) == 0) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "Device %s does not need activation",
			     fu_device_get_id (device));
		return FALSE;
	}

	/* run vfunc */
	if (!fu_plugin_runner_device_generic (self, device,
					      "fu_plugin_activate", error))
		return FALSE;

	/* update with correct flags */
	fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	fu_device_set_modified (device, (guint64) g_get_real_time () / G_USEC_PER_SEC);
	return TRUE;
}

gboolean
fu_plugin_runner_unlock (FuPlugin *self, FuDevice *device, GError **error)
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
	if (!fu_plugin_runner_device_generic (self, device,
					      "fu_plugin_unlock", error))
		return FALSE;

	/* update with correct flags */
	flags = fu_device_get_flags (device);
	fu_device_set_flags (device, flags &= ~FWUPD_DEVICE_FLAG_LOCKED);
	fu_device_set_modified (device, (guint64) g_get_real_time () / G_USEC_PER_SEC);
	return TRUE;
}

gboolean
fu_plugin_runner_update (FuPlugin *self,
			 FuDevice *device,
			 GBytes *blob_fw,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginUpdateFunc update_func;
	g_autoptr(FuHistory) history = NULL;
	g_autoptr(FuDevice) device_pending = NULL;
	g_autoptr(GError) error_local = NULL;

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

	/* cancel the pending action */
	if (!fu_plugin_runner_offline_invalidate (error))
		return FALSE;

	/* online */
	history = fu_history_new ();
	device_pending = fu_history_get_device_by_id (history, fu_device_get_id (device), NULL);
	if (!update_func (self, device, blob_fw, flags, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset error in plugin %s for update()",
				    priv->name);
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
			return FALSE;
		}
		fu_device_set_update_error (device, error_local->message);
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}

	/* no longer valid */
	if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT) &&
	    !fu_device_has_flag (device, FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN)) {
		GPtrArray *checksums = fu_device_get_checksums (device);
		g_ptr_array_set_size (checksums, 0);
	}

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
			g_autoptr(GError) error_delete = NULL;
			g_autoptr(GFile) file = NULL;
			file = g_file_new_for_path (tmp);
			if (!g_file_delete (file, NULL, &error_delete)) {
				g_set_error (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INVALID_FILE,
					     "Failed to delete %s: %s",
					     tmp, error_delete->message);
				return FALSE;
			}
		}
	}
	return TRUE;
}

gboolean
fu_plugin_runner_clear_results (FuPlugin *self, FuDevice *device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginDeviceFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

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
	if (!func (self, device, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset error in plugin %s for clear_result()",
				    priv->name);
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to clear_result using %s: ",
					    priv->name);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_plugin_runner_get_results (FuPlugin *self, FuDevice *device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginDeviceFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

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
	if (!func (self, device, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset error in plugin %s for get_results()",
				    priv->name);
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to get_results using %s: ",
					    priv->name);
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_plugin_get_order:
 * @self: a #FuPlugin
 *
 * Gets the plugin order, where higher numbers are run after lower
 * numbers.
 *
 * Returns: the integer value
 **/
guint
fu_plugin_get_order (FuPlugin *self)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (self);
	return priv->order;
}

/**
 * fu_plugin_set_order:
 * @self: a #FuPlugin
 * @order: a integer value
 *
 * Sets the plugin order, where higher numbers are run after lower
 * numbers.
 **/
void
fu_plugin_set_order (FuPlugin *self, guint order)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (self);
	priv->order = order;
}

/**
 * fu_plugin_get_priority:
 * @self: a #FuPlugin
 *
 * Gets the plugin priority, where higher numbers are better.
 *
 * Returns: the integer value
 **/
guint
fu_plugin_get_priority (FuPlugin *self)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (self);
	return priv->priority;
}

/**
 * fu_plugin_set_priority:
 * @self: a #FuPlugin
 * @priority: a integer value
 *
 * Sets the plugin priority, where higher numbers are better.
 **/
void
fu_plugin_set_priority (FuPlugin *self, guint priority)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (self);
	priv->priority = priority;
}

/**
 * fu_plugin_add_rule:
 * @self: a #FuPlugin
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
fu_plugin_add_rule (FuPlugin *self, FuPluginRule rule, const gchar *name)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (self);
	g_ptr_array_add (priv->rules[rule], g_strdup (name));
	g_signal_emit (self, signals[SIGNAL_RULES_CHANGED], 0);
}

/**
 * fu_plugin_get_rules:
 * @self: a #FuPlugin
 * @rule: a #FuPluginRule, e.g. %FU_PLUGIN_RULE_CONFLICTS
 *
 * Gets the plugin IDs that should be run after this plugin.
 *
 * Returns: (element-type utf8) (transfer none): the list of plugin names, e.g. ['appstream']
 **/
GPtrArray *
fu_plugin_get_rules (FuPlugin *self, FuPluginRule rule)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (self);
	return priv->rules[rule];
}

/**
 * fu_plugin_has_rule:
 * @self: a #FuPlugin
 * @rule: a #FuPluginRule, e.g. %FU_PLUGIN_RULE_CONFLICTS
 * @name: a plugin name, e.g. `upower`
 *
 * Gets the plugin IDs that should be run after this plugin.
 *
 * Returns: %TRUE if the name exists for the specific rule
 **/
gboolean
fu_plugin_has_rule (FuPlugin *self, FuPluginRule rule, const gchar *name)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (self);
	for (guint i = 0; i < priv->rules[rule]->len; i++) {
		const gchar *tmp = g_ptr_array_index (priv->rules[rule], i);
		if (g_strcmp0 (tmp, name) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * fu_plugin_add_report_metadata:
 * @self: a #FuPlugin
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
fu_plugin_add_report_metadata (FuPlugin *self, const gchar *key, const gchar *value)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (self);
	g_hash_table_insert (priv->report_metadata, g_strdup (key), g_strdup (value));
}

/**
 * fu_plugin_get_report_metadata:
 * @self: a #FuPlugin
 *
 * Returns the list of additional metadata to be added when filing a report.
 *
 * Returns: (transfer none): the map of report metadata
 **/
GHashTable *
fu_plugin_get_report_metadata (FuPlugin *self)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (self);
	return priv->report_metadata;
}

/**
 * fu_plugin_get_config_value:
 * @self: a #FuPlugin
 * @key: A settings key
 *
 * Return the value of a key if it's been configured
 *
 * Since: 1.0.6
 **/
gchar *
fu_plugin_get_config_value (FuPlugin *self, const gchar *key)
{
	g_autofree gchar *conf_dir = NULL;
	g_autofree gchar *conf_file = NULL;
	g_autofree gchar *conf_path = NULL;
	g_autoptr(GKeyFile) keyfile = NULL;
	const gchar *plugin_name;

	conf_dir = fu_common_get_path (FU_PATH_KIND_SYSCONFDIR_PKG);
	plugin_name = fu_plugin_get_name (self);
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
	signals[SIGNAL_CHECK_SUPPORTED] =
		g_signal_new ("check-supported",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuPluginClass, check_supported),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_BOOLEAN, 1, G_TYPE_STRING);
	signals[SIGNAL_RULES_CHANGED] =
		g_signal_new ("rules-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FuPluginClass, rules_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

static void
fu_plugin_init (FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	priv->enabled = TRUE;
	priv->udev_subsystems = g_ptr_array_new_with_free_func (g_free);
	priv->devices = g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free, (GDestroyNotify) g_object_unref);
	g_rw_lock_init (&priv->devices_mutex);
	priv->report_metadata = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	for (guint i = 0; i < FU_PLUGIN_RULE_LAST; i++)
		priv->rules[i] = g_ptr_array_new_with_free_func (g_free);
}

static void
fu_plugin_finalize (GObject *object)
{
	FuPlugin *self = FU_PLUGIN (object);
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginInitFunc func = NULL;

	/* optional */
	if (priv->module != NULL) {
		g_module_symbol (priv->module, "fu_plugin_destroy", (gpointer *) &func);
		if (func != NULL) {
			g_debug ("performing destroy() on %s", priv->name);
			func (self);
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
	if (priv->udev_subsystems != NULL)
		g_ptr_array_unref (priv->udev_subsystems);
	if (priv->smbios != NULL)
		g_object_unref (priv->smbios);
	if (priv->runtime_versions != NULL)
		g_hash_table_unref (priv->runtime_versions);
	if (priv->compile_versions != NULL)
		g_hash_table_unref (priv->compile_versions);
	g_hash_table_unref (priv->devices);
	g_hash_table_unref (priv->report_metadata);
	g_rw_lock_clear (&priv->devices_mutex);
	g_free (priv->build_hash);
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
	return FU_PLUGIN (g_object_new (FU_TYPE_PLUGIN, NULL));
}

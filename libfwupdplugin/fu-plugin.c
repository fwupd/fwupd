/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-plugin-private.h"
#include "fu-mutex.h"

/**
 * SECTION:fu-plugin
 * @short_description: a daemon plugin
 *
 * An object that represents a plugin run by the daemon.
 *
 * See also: #FuDevice
 */

static void fu_plugin_finalize			 (GObject *object);

typedef struct {
	GModule			*module;
	guint			 order;
	guint			 priority;
	GPtrArray		*rules[FU_PLUGIN_RULE_LAST];
	GPtrArray		*devices;		/* (nullable) (element-type FuDevice) */
	gchar			*build_hash;
	GHashTable		*runtime_versions;
	GHashTable		*compile_versions;
	FuContext		*ctx;
	GArray			*device_gtypes;		/* (nullable): of #GType */
	GHashTable		*cache;			/* (nullable): platform_id:GObject */
	GRWLock			 cache_mutex;
	GHashTable		*report_metadata;	/* (nullable): key:value */
	FuPluginData		*data;
} FuPluginPrivate;

enum {
	SIGNAL_DEVICE_ADDED,
	SIGNAL_DEVICE_REMOVED,
	SIGNAL_DEVICE_REGISTER,
	SIGNAL_RULES_CHANGED,
	SIGNAL_CHECK_SUPPORTED,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE_WITH_PRIVATE (FuPlugin, fu_plugin, FWUPD_TYPE_PLUGIN)
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
typedef void		 (*FuPluginSecurityAttrsFunc)	(FuPlugin	*self,
							 FuSecurityAttrs *attrs);

/**
 * fu_plugin_is_open:
 * @self: a #FuPlugin
 *
 * Determines if the plugin is opened
 *
 * Returns: TRUE for opened, FALSE for not
 *
 * Since: 1.3.5
 **/
gboolean
fu_plugin_is_open (FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	return priv->module != NULL;
}

/**
 * fu_plugin_get_name:
 * @self: a #FuPlugin
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
	g_return_val_if_fail (FU_IS_PLUGIN (self), NULL);
	return fwupd_plugin_get_name (FWUPD_PLUGIN (self));
}

/**
 * fu_plugin_set_name:
 * @self: a #FuPlugin
 * @name: a string
 *
 * Sets the plugin name.
 *
 * Since: 0.8.0
 **/
void
fu_plugin_set_name (FuPlugin *self, const gchar *name)
{
	g_return_if_fail (FU_IS_PLUGIN (self));
	fwupd_plugin_set_name (FWUPD_PLUGIN (self), name);
}

/**
 * fu_plugin_set_build_hash:
 * @self: a #FuPlugin
 * @build_hash: a checksum
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

	/* not changed */
	if (g_strcmp0 (priv->build_hash, build_hash) == 0)
		return;

	g_free (priv->build_hash);
	priv->build_hash = g_strdup (build_hash);
}

/**
 * fu_plugin_get_build_hash:
 * @self: a #FuPlugin
 *
 * Gets the build hash a plugin was generated with.
 *
 * Returns: (transfer none): a #gchar, or %NULL for unset.
 *
 * Since: 1.2.4
 **/
const gchar *
fu_plugin_get_build_hash (FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_PLUGIN (self), NULL);
	return priv->build_hash;
}

/**
 * fu_plugin_cache_lookup:
 * @self: a #FuPlugin
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
	g_autoptr(GRWLockReaderLocker) locker = g_rw_lock_reader_locker_new (&priv->cache_mutex);
	g_return_val_if_fail (FU_IS_PLUGIN (self), NULL);
	g_return_val_if_fail (id != NULL, NULL);
	g_return_val_if_fail (locker != NULL, NULL);
	if (priv->cache == NULL)
		return NULL;
	return g_hash_table_lookup (priv->cache, id);
}

/**
 * fu_plugin_cache_add:
 * @self: a #FuPlugin
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
	g_autoptr(GRWLockWriterLocker) locker = g_rw_lock_writer_locker_new (&priv->cache_mutex);
	g_return_if_fail (FU_IS_PLUGIN (self));
	g_return_if_fail (id != NULL);
	g_return_if_fail (G_IS_OBJECT (dev));
	g_return_if_fail (locker != NULL);
	if (priv->cache == NULL) {
		priv->cache = g_hash_table_new_full (g_str_hash,
						     g_str_equal,
						     g_free,
						     (GDestroyNotify) g_object_unref);
	}
	g_hash_table_insert (priv->cache, g_strdup (id), g_object_ref (dev));
}

/**
 * fu_plugin_cache_remove:
 * @self: a #FuPlugin
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
	g_autoptr(GRWLockWriterLocker) locker = g_rw_lock_writer_locker_new (&priv->cache_mutex);
	g_return_if_fail (FU_IS_PLUGIN (self));
	g_return_if_fail (id != NULL);
	g_return_if_fail (locker != NULL);
	if (priv->cache == NULL)
		return;
	g_hash_table_remove (priv->cache, id);
}

/**
 * fu_plugin_get_data:
 * @self: a #FuPlugin
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
 * fu_plugin_alloc_data: (skip):
 * @self: a #FuPlugin
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
 * fu_plugin_guess_name_from_fn:
 * @filename: filename to guess
 *
 * Tries to guess the name of the plugin from a filename
 *
 * Returns: (transfer full): the guessed name of the plugin
 *
 * Since: 1.0.8
 **/
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

/**
 * fu_plugin_open:
 * @self: a #FuPlugin
 * @filename: the shared object filename to open
 * @error: (nullable): optional return location for an error
 *
 * Opens the plugin module
 *
 * Returns: TRUE for success, FALSE for fail
 *
 * Since: 0.8.0
 **/
gboolean
fu_plugin_open (FuPlugin *self, const gchar *filename, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginInitFunc func = NULL;

	g_return_val_if_fail (FU_IS_PLUGIN (self), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	priv->module = g_module_open (filename, 0);
	if (priv->module == NULL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "failed to open plugin %s: %s",
			     filename, g_module_error ());
		fu_plugin_add_flag (self, FWUPD_PLUGIN_FLAG_FAILED_OPEN);
		fu_plugin_add_flag (self, FWUPD_PLUGIN_FLAG_USER_WARNING);
		return FALSE;
	}

	/* set automatically */
	if (fu_plugin_get_name (self) == NULL) {
		g_autofree gchar *str = fu_plugin_guess_name_from_fn (filename);
		fu_plugin_set_name (self, str);
	}

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_init", (gpointer *) &func);
	if (func != NULL) {
		g_debug ("init(%s)", filename);
		func (self);
	}

	return TRUE;
}

/* order of usefulness to the user */
static const gchar *
fu_plugin_build_device_update_error (FuPlugin *self)
{
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_NO_HARDWARE))
		return "Not updatable as required hardware was not found";
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_LEGACY_BIOS))
		return "Not updatable in legacy BIOS mode";
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_CAPSULES_UNSUPPORTED))
		return "Not updatable as UEFI capsule updates not enabled in firmware setup";
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_UNLOCK_REQUIRED))
		return "Not updatable as requires unlock";
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_EFIVAR_NOT_MOUNTED))
		return "Not updatable as efivarfs was not found";
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_ESP_NOT_FOUND))
		return "Not updatable as UEFI ESP partition not detected";
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_DISABLED))
		return "Not updatable as plugin was disabled";
	return NULL;
}

static void
fu_plugin_ensure_devices (FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	if (priv->devices != NULL)
		return;
	priv->devices = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

/**
 * fu_plugin_device_add:
 * @self: a #FuPlugin
 * @device: a device
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
	FuPluginPrivate *priv = GET_PRIVATE (self);
	GPtrArray *children;
	g_autoptr(GError) error = NULL;

	g_return_if_fail (FU_IS_PLUGIN (self));
	g_return_if_fail (FU_IS_DEVICE (device));

	/* ensure the device ID is set from the physical and logical IDs */
	if (!fu_device_ensure_id (device, &error)) {
		g_warning ("ignoring add: %s", error->message);
		return;
	}

	/* add to array */
	fu_plugin_ensure_devices (self);
	g_ptr_array_add (priv->devices, g_object_ref (device));

	/* proxy to device where required */
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE)) {
		g_debug ("plugin %s has _CLEAR_UPDATABLE, so removing from %s",
			 fu_plugin_get_name (self),
			 fu_device_get_id (device));
		fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);
	}
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_USER_WARNING) &&
	    fu_device_get_update_error (device) == NULL) {
		const gchar *tmp = fu_plugin_build_device_update_error (self);
		g_debug ("setting %s update error to '%s' from %s",
			 fu_device_get_id (device), tmp, fu_plugin_get_name (self));
		fu_device_set_update_error (device, tmp);
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
 * fu_plugin_get_devices:
 * @self: a #FuPlugin
 *
 * Returns all devices added by the plugin using fu_plugin_device_add() and
 * not yet removed with fu_plugin_device_remove().
 *
 * Returns: (transfer none) (element-type FuDevice): devices
 *
 * Since: 1.5.6
 **/
GPtrArray *
fu_plugin_get_devices (FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_return_val_if_fail (FU_IS_PLUGIN (self), NULL);
	fu_plugin_ensure_devices (self);
	return priv->devices;
}

/**
 * fu_plugin_device_register:
 * @self: a #FuPlugin
 * @device: a device
 *
 * Registers the device with other plugins so they can set metadata.
 *
 * Plugins do not have to call this manually as this is done automatically
 * when using fu_plugin_device_add(). They may wish to use this manually
 * if for instance the coldplug should be ignored based on the metadata
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
 * @self: a #FuPlugin
 * @device: a device
 *
 * Asks the daemon to remove a device from the exported list.
 *
 * Since: 0.8.0
 **/
void
fu_plugin_device_remove (FuPlugin *self, FuDevice *device)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);

	g_return_if_fail (FU_IS_PLUGIN (self));
	g_return_if_fail (FU_IS_DEVICE (device));

	/* remove from array */
	if (priv->devices != NULL)
		g_ptr_array_remove (priv->devices, device);

	g_debug ("emit removed from %s: %s",
		 fu_plugin_get_name (self),
		 fu_device_get_id (device));
	g_signal_emit (self, signals[SIGNAL_DEVICE_REMOVED], 0, device);
}

/**
 * fu_plugin_has_custom_flag:
 * @self: a #FuPlugin
 * @flag: a custom text flag, specific to the plugin, e.g. `uefi-force-enable`
 *
 * Returns if a per-plugin HwId custom flag exists, typically added from a DMI quirk.
 *
 * Returns: %TRUE if the quirk entry exists
 *
 * Since: 1.3.1
 **/
gboolean
fu_plugin_has_custom_flag (FuPlugin *self, const gchar *flag)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	GPtrArray *guids;

	g_return_val_if_fail (FU_IS_PLUGIN (self), FALSE);
	g_return_val_if_fail (flag != NULL, FALSE);

	/* never set up, e.g. in tests */
	if (priv->ctx == NULL)
		return FALSE;

	/* search each hwid */
	guids = fu_context_get_hwid_guids (priv->ctx);
	for (guint i = 0; i < guids->len; i++) {
		const gchar *guid = g_ptr_array_index (guids, i);
		const gchar *value;

		/* does prefixed quirk exist */
		value = fu_context_lookup_quirk_by_id (priv->ctx, guid, FU_QUIRKS_FLAGS);
		if (value != NULL) {
			g_auto(GStrv) values = g_strsplit (value, ",", -1);
			if (g_strv_contains ((const gchar * const *) values, flag))
				return TRUE;
		}
	}
	return FALSE;
}

/**
 * fu_plugin_check_supported:
 * @self: a #FuPlugin
 * @guid: a hardware ID GUID, e.g. `6de5d951-d755-576b-bd09-c5cf66b27234`
 *
 * Checks to see if a specific device GUID is supported, i.e. available in the
 * AppStream metadata.
 *
 * Returns: %TRUE if the device is supported.
 *
 * Since: 1.0.0
 **/
static gboolean
fu_plugin_check_supported (FuPlugin *self, const gchar *guid)
{
	gboolean retval = FALSE;
	g_signal_emit (self, signals[SIGNAL_CHECK_SUPPORTED], 0, guid, &retval);
	return retval;
}

/**
 * fu_plugin_get_context:
 * @self: a #FuPlugin
 *
 * Gets the context for a plugin.
 *
 * Returns: (transfer none): a #FuContext or %NULL if not set
 *
 * Since: 1.6.0
 **/
FuContext *
fu_plugin_get_context (FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	return priv->ctx;
}

static gboolean
fu_plugin_device_attach (FuPlugin *self, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_attach (device, error);
}

static gboolean
fu_plugin_device_detach (FuPlugin *self, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_detach (device, error);
}

static gboolean
fu_plugin_device_activate (FuPlugin *self, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_activate (device, error);
}

static gboolean
fu_plugin_device_write_firmware (FuPlugin *self, FuDevice *device,
				 GBytes *fw, FwupdInstallFlags flags,
				 GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;

	/* back the old firmware up to /var/lib/fwupd */
	if (fu_device_has_flag (device, FWUPD_DEVICE_FLAG_BACKUP_BEFORE_INSTALL)) {
		g_autoptr(GBytes) fw_old = NULL;
		g_autofree gchar *path = NULL;
		g_autofree gchar *fn = NULL;
		g_autofree gchar *localstatedir = NULL;

		fw_old = fu_device_dump_firmware (device, error);
		if (fw_old == NULL) {
			g_prefix_error (error, "failed to backup old firmware: ");
			return FALSE;
		}
		localstatedir = fu_common_get_path (FU_PATH_KIND_LOCALSTATEDIR_PKG);
		fn = g_strdup_printf ("%s.bin", fu_device_get_version (device));
		path = g_build_filename (localstatedir,
					 "backup",
					 fu_device_get_id (device),
					 fu_device_get_serial (device) != NULL ?
						fu_device_get_serial (device) :
						"default",
					 fn, NULL);
		if (!fu_common_set_contents_bytes (path, fw_old, error))
			return FALSE;
	}

	return fu_device_write_firmware (device, fw, flags, error);
}

static gboolean
fu_plugin_device_read_firmware (FuPlugin *self, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) fw = NULL;
	GChecksumType checksum_types[] = {
		G_CHECKSUM_SHA1,
		G_CHECKSUM_SHA256,
		0 };
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_device_detach (device, error))
		return FALSE;
	firmware = fu_device_read_firmware (device, error);
	if (firmware == NULL) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_device_attach (device, &error_local))
			g_debug ("ignoring attach failure: %s", error_local->message);
		g_prefix_error (error, "failed to read firmware: ");
		return FALSE;
	}
	fw = fu_firmware_write (firmware, error);
	if (fw == NULL) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_device_attach (device, &error_local))
			g_debug ("ignoring attach failure: %s", error_local->message);
		g_prefix_error (error, "failed to write firmware: ");
		return FALSE;
	}
	for (guint i = 0; checksum_types[i] != 0; i++) {
		g_autofree gchar *hash = NULL;
		hash = g_compute_checksum_for_bytes (checksum_types[i], fw);
		fu_device_add_checksum (device, hash);
	}
	return fu_device_attach (device, error);
}

/**
 * fu_plugin_runner_startup:
 * @self: a #FuPlugin
 * @error: (nullable): optional return location for an error
 *
 * Runs the startup routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 0.8.0
 **/
gboolean
fu_plugin_runner_startup (FuPlugin *self, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginStartupFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

	/* not enabled */
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_startup", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("startup(%s)", fu_plugin_get_name (self));
	if (!func (self, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset plugin error in startup(%s)",
				    fu_plugin_get_name (self));
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to startup using %s: ",
					    fu_plugin_get_name (self));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_plugin_runner_device_generic (FuPlugin *self, FuDevice *device,
				 const gchar *symbol_name,
				 FuPluginDeviceFunc device_func,
				 GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginDeviceFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

	/* not enabled */
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, symbol_name, (gpointer *) &func);
	if (func == NULL) {
		if (device_func != NULL) {
			g_debug ("running superclassed %s(%s)",
				 symbol_name + 10, fu_plugin_get_name (self));
			return device_func (self, device, error);
		}
		return TRUE;
	}
	g_debug ("%s(%s)", symbol_name + 10, fu_plugin_get_name (self));
	if (!func (self, device, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset plugin error in %s(%s)",
				    fu_plugin_get_name (self), symbol_name + 10);
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to %s using %s: ",
					    symbol_name + 10, fu_plugin_get_name (self));
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
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, symbol_name, (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("%s(%s)", symbol_name + 10, fu_plugin_get_name (self));
	if (!func (self, flags, device, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset plugin error in %s(%s)",
				    fu_plugin_get_name (self), symbol_name + 10);
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to %s using %s: ",
					    symbol_name + 10, fu_plugin_get_name (self));
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
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, symbol_name, (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("%s(%s)", symbol_name + 10, fu_plugin_get_name (self));
	if (!func (self, devices, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset plugin error in for %s(%s)",
				    fu_plugin_get_name (self), symbol_name + 10);
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to %s using %s: ",
					    symbol_name + 10, fu_plugin_get_name (self));
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_plugin_runner_coldplug:
 * @self: a #FuPlugin
 * @error: (nullable): optional return location for an error
 *
 * Runs the coldplug routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 0.8.0
 **/
gboolean
fu_plugin_runner_coldplug (FuPlugin *self, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginStartupFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

	/* not enabled */
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* no HwId */
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_REQUIRE_HWID))
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_coldplug", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("coldplug(%s)", fu_plugin_get_name (self));
	if (!func (self, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset plugin error in coldplug(%s)",
				    fu_plugin_get_name (self));
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to coldplug using %s: ", fu_plugin_get_name (self));
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_plugin_runner_coldplug_prepare:
 * @self: a #FuPlugin
 * @error: (nullable): optional return location for an error
 *
 * Runs the coldplug_prepare routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 0.8.0
 **/
gboolean
fu_plugin_runner_coldplug_prepare (FuPlugin *self, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginStartupFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

	/* not enabled */
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_coldplug_prepare", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("coldplug_prepare(%s)", fu_plugin_get_name (self));
	if (!func (self, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset plugin error in coldplug_prepare(%s)",
				    fu_plugin_get_name (self));
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to coldplug_prepare using %s: ",
					    fu_plugin_get_name (self));
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_plugin_runner_coldplug_cleanup:
 * @self: a #FuPlugin
 * @error: (nullable): optional return location for an error
 *
 * Runs the coldplug_cleanup routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 0.8.0
 **/
gboolean
fu_plugin_runner_coldplug_cleanup (FuPlugin *self, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginStartupFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

	/* not enabled */
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_coldplug_cleanup", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("coldplug_cleanup(%s)", fu_plugin_get_name (self));
	if (!func (self, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset plugin error in coldplug_cleanup(%s)",
				    fu_plugin_get_name (self));
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to coldplug_cleanup using %s: ",
					    fu_plugin_get_name (self));
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_plugin_runner_composite_prepare:
 * @self: a #FuPlugin
 * @devices: (element-type FuDevice): an array of devices
 * @error: (nullable): optional return location for an error
 *
 * Runs the composite_prepare routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.0.9
 **/
gboolean
fu_plugin_runner_composite_prepare (FuPlugin *self, GPtrArray *devices, GError **error)
{
	return fu_plugin_runner_device_array_generic (self, devices,
						      "fu_plugin_composite_prepare",
						      error);
}

/**
 * fu_plugin_runner_composite_cleanup:
 * @self: a #FuPlugin
 * @devices: (element-type FuDevice): an array of devices
 * @error: (nullable): optional return location for an error
 *
 * Runs the composite_cleanup routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.0.9
 **/
gboolean
fu_plugin_runner_composite_cleanup (FuPlugin *self, GPtrArray *devices, GError **error)
{
	return fu_plugin_runner_device_array_generic (self, devices,
						      "fu_plugin_composite_cleanup",
						      error);
}

/**
 * fu_plugin_runner_update_prepare:
 * @self: a #FuPlugin
 * @flags: install flags
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Runs the update_prepare routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.1.2
 **/
gboolean
fu_plugin_runner_update_prepare (FuPlugin *self, FwupdInstallFlags flags, FuDevice *device,
				 GError **error)
{
	return fu_plugin_runner_flagged_device_generic (self, flags, device,
							"fu_plugin_update_prepare",
							error);
}

/**
 * fu_plugin_runner_update_cleanup:
 * @self: a #FuPlugin
 * @flags: install flags
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Runs the update_cleanup routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.1.2
 **/
gboolean
fu_plugin_runner_update_cleanup (FuPlugin *self, FwupdInstallFlags flags, FuDevice *device,
				 GError **error)
{
	return fu_plugin_runner_flagged_device_generic (self, flags, device,
							"fu_plugin_update_cleanup",
							error);
}

/**
 * fu_plugin_runner_update_attach:
 * @self: a #FuPlugin
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Runs the update_attach routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.1.2
 **/
gboolean
fu_plugin_runner_update_attach (FuPlugin *self, FuDevice *device, GError **error)
{
	return fu_plugin_runner_device_generic (self, device,
						"fu_plugin_update_attach",
						fu_plugin_device_attach,
						error);
}

/**
 * fu_plugin_runner_update_detach:
 * @self: a #FuPlugin
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Runs the update_detach routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.1.2
 **/
gboolean
fu_plugin_runner_update_detach (FuPlugin *self, FuDevice *device, GError **error)
{
	return fu_plugin_runner_device_generic (self, device,
						"fu_plugin_update_detach",
						fu_plugin_device_detach,
						error);
}

/**
 * fu_plugin_runner_update_reload:
 * @self: a #FuPlugin
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Runs reload routine for a device
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.1.2
 **/
gboolean
fu_plugin_runner_update_reload (FuPlugin *self, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* not enabled */
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* no object loaded */
	locker = fu_device_locker_new (device, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_reload (device, error);
}

/**
 * fu_plugin_runner_add_security_attrs:
 * @self: a #FuPlugin
 * @attrs: a security attribute
 *
 * Runs the `add_security_attrs()` routine for the plugin
 *
 * Since: 1.5.0
 **/
void
fu_plugin_runner_add_security_attrs (FuPlugin *self, FuSecurityAttrs *attrs)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginSecurityAttrsFunc func = NULL;
	const gchar *symbol_name = "fu_plugin_add_security_attrs";

	/* no object loaded */
	if (priv->module == NULL)
		return;

	/* optional, but gets called even for disabled plugins */
	g_module_symbol (priv->module, symbol_name, (gpointer *) &func);
	if (func == NULL)
		return;
	g_debug ("%s(%s)", symbol_name + 10, fu_plugin_get_name (self));
	func (self, attrs);
}

/**
 * fu_plugin_add_device_gtype:
 * @self: a #FuPlugin
 * @device_gtype: a #GType, e.g. `FU_TYPE_DEVICE`
 *
 * Adds the device #GType which is used when creating devices.
 *
 * If this method is used then fu_plugin_backend_device_added() is not called, and
 * instead the object is created in the daemon for the plugin.
 *
 * Plugins can use this method only in fu_plugin_init()
 *
 * Since: 1.6.0
 **/
void
fu_plugin_add_device_gtype (FuPlugin *self, GType device_gtype)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);

	/* create as required */
	if (priv->device_gtypes == NULL)
		priv->device_gtypes = g_array_new (FALSE, FALSE, sizeof(GType));

	/* ensure (to allow quirks to use it) then add */
	g_type_ensure (device_gtype);
	g_array_append_val (priv->device_gtypes, device_gtype);
}

static gchar *
fu_common_string_uncamelcase (const gchar *str)
{
	GString *tmp = g_string_new (NULL);
	for (guint i = 0; str[i] != '\0'; i++) {
		if (g_ascii_islower (str[i]) ||
		    g_ascii_isdigit (str[i])) {
			g_string_append_c (tmp, str[i]);
			continue;
		}
		if (i > 0)
			g_string_append_c (tmp, '-');
		g_string_append_c (tmp, g_ascii_tolower (str[i]));
	}
	return g_string_free (tmp, FALSE);
}

/**
 * fu_plugin_add_firmware_gtype:
 * @self: a #FuPlugin
 * @id: (nullable): an optional string describing the type, e.g. `ihex`
 * @gtype: a #GType e.g. `FU_TYPE_FOO_FIRMWARE`
 *
 * Adds a firmware #GType which is used when creating devices. If @id is not
 * specified then it is guessed using the #GType name.
 *
 * Plugins can use this method only in fu_plugin_init()
 *
 * Since: 1.3.3
 **/
void
fu_plugin_add_firmware_gtype (FuPlugin *self, const gchar *id, GType gtype)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	g_autofree gchar *id_safe = NULL;
	if (id != NULL) {
		id_safe = g_strdup (id);
	} else {
		g_autoptr(GString) str = g_string_new (g_type_name (gtype));
		if (g_str_has_prefix (str->str, "Fu"))
			g_string_erase (str, 0, 2);
		fu_common_string_replace (str, "Firmware", "");
		id_safe = fu_common_string_uncamelcase (str->str);
	}
	fu_context_add_firmware_gtype (priv->ctx, id_safe, gtype);
}

static gboolean
fu_plugin_check_supported_device (FuPlugin *self, FuDevice *device)
{
	GPtrArray *instance_ids = fu_device_get_instance_ids (device);
	for (guint i = 0; i < instance_ids->len; i++) {
		const gchar *instance_id = g_ptr_array_index (instance_ids, i);
		g_autofree gchar *guid = fwupd_guid_hash_string (instance_id);
		if (fu_plugin_check_supported (self, guid))
			return TRUE;
	}
	return FALSE;
}

static gboolean
fu_plugin_backend_device_added (FuPlugin *self, FuDevice *device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	GType device_gtype = fu_device_get_specialized_gtype (FU_DEVICE (device));
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* fall back to plugin default */
	if (device_gtype == G_TYPE_INVALID) {
		if (priv->device_gtypes->len > 1) {
			g_set_error_literal (error,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "too many GTypes to choose a default");
			return FALSE;
		}
		device_gtype = g_array_index (priv->device_gtypes, GType, 0);
	}

	/* create new device and incorporate existing properties */
	dev = g_object_new (device_gtype, NULL);
	fu_device_incorporate (dev, FU_DEVICE (device));
	if (!fu_plugin_runner_device_created (self, dev, error))
		return FALSE;

	/* there are a lot of different devices that match, but not all respond
	 * well to opening -- so limit some ones with issued updates */
	if (fu_device_has_internal_flag (dev, FU_DEVICE_INTERNAL_FLAG_ONLY_SUPPORTED)) {
		if (!fu_device_probe (dev, error))
			return FALSE;
		fu_device_convert_instance_ids (dev);
		if (!fu_plugin_check_supported_device (self, dev)) {
			g_autofree gchar *guids = fu_device_get_guids_as_str (dev);
			g_debug ("%s has no updates, so ignoring device", guids);
			return TRUE;
		}
	}

	/* open and add */
	locker = fu_device_locker_new (dev, error);
	if (locker == NULL)
		return FALSE;
	fu_plugin_device_add (self, dev);
	fu_plugin_runner_device_added (self, dev);
	return TRUE;
}

/**
 * fu_plugin_runner_backend_device_added:
 * @self: a #FuPlugin
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Call the backend_device_added routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.5.6
 **/
gboolean
fu_plugin_runner_backend_device_added (FuPlugin *self, FuDevice *device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginDeviceFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (FU_IS_PLUGIN (self), FALSE);
	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not enabled */
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_backend_device_added", (gpointer *) &func);
	if (func == NULL) {
		if (priv->device_gtypes != NULL ||
		    fu_device_get_specialized_gtype (device) != G_TYPE_INVALID) {
			return fu_plugin_backend_device_added (self, device, error);
		}
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "No device GType set");
		return FALSE;
	}
	g_debug ("backend_device_added(%s)", fu_plugin_get_name (self));
	if (!func (self, device, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset plugin error in backend_device_added(%s)",
				    fu_plugin_get_name (self));
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to add device using on %s: ",
					    fu_plugin_get_name (self));
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_plugin_runner_backend_device_changed:
 * @self: a #FuPlugin
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Call the backend_device_changed routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.5.6
 **/
gboolean
fu_plugin_runner_backend_device_changed (FuPlugin *self, FuDevice *device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginDeviceFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (FU_IS_PLUGIN (self), FALSE);
	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not enabled */
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_backend_device_changed", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("udev_device_changed(%s)", fu_plugin_get_name (self));
	if (!func (self, device, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset plugin error in udev_device_changed(%s)",
				    fu_plugin_get_name (self));
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to change device on %s: ",
					    fu_plugin_get_name (self));
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_plugin_runner_device_added:
 * @self: a #FuPlugin
 * @device: a device
 *
 * Call the device_added routine for the plugin
 *
 * Since: 1.5.0
 **/
void
fu_plugin_runner_device_added (FuPlugin *self, FuDevice *device)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginDeviceRegisterFunc func = NULL;

	/* not enabled */
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_DISABLED))
		return;
	if (priv->module == NULL)
		return;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_device_added", (gpointer *) &func);
	if (func == NULL)
		return;
	g_debug ("fu_plugin_device_added(%s)", fu_plugin_get_name (self));
	func (self, device);
}

/**
 * fu_plugin_runner_device_removed:
 * @self: a #FuPlugin
 * @device: a device
 *
 * Call the device_removed routine for the plugin
 *
 * Since: 1.1.2
 **/
void
fu_plugin_runner_device_removed (FuPlugin *self, FuDevice *device)
{
	g_autoptr(GError) error_local= NULL;

	if (!fu_plugin_runner_device_generic (self, device,
					      "fu_plugin_backend_device_removed",
					      NULL,
					      &error_local))
		g_warning ("%s", error_local->message);
}

/**
 * fu_plugin_runner_device_register:
 * @self: a #FuPlugin
 * @device: a device
 *
 * Call the device_registered routine for the plugin
 *
 * Since: 0.9.7
 **/
void
fu_plugin_runner_device_register (FuPlugin *self, FuDevice *device)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginDeviceRegisterFunc func = NULL;

	/* not enabled */
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_DISABLED))
		return;
	if (priv->module == NULL)
		return;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_device_registered", (gpointer *) &func);
	if (func != NULL) {
		g_debug ("fu_plugin_device_registered(%s)", fu_plugin_get_name (self));
		func (self, device);
	}
}

/**
 * fu_plugin_runner_device_created:
 * @self: a #FuPlugin
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Call the device_created routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.4.0
 **/
gboolean
fu_plugin_runner_device_created (FuPlugin *self, FuDevice *device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginDeviceFunc func = NULL;

	g_return_val_if_fail (FU_IS_PLUGIN (self), FALSE);
	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not enabled */
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_device_created", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("fu_plugin_device_created(%s)", fu_plugin_get_name (self));
	return func (self, device, error);
}

/**
 * fu_plugin_runner_verify:
 * @self: a #FuPlugin
 * @device: a device
 * @flags: verify flags
 * @error: (nullable): optional return location for an error
 *
 * Call into the plugin's verify routine
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 0.8.0
 **/
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

	g_return_val_if_fail (FU_IS_PLUGIN (self), FALSE);
	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not enabled */
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_verify", (gpointer *) &func);
	if (func == NULL) {
		if (!fu_device_has_flag (device, FWUPD_DEVICE_FLAG_CAN_VERIFY)) {
			g_set_error (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     "device %s does not support verification",
				     fu_device_get_id (device));
			return FALSE;
		}
		return fu_plugin_device_read_firmware (self, device, error);
	}

	/* clear any existing verification checksums */
	checksums = fu_device_get_checksums (device);
	g_ptr_array_set_size (checksums, 0);

	/* run additional detach */
	if (!fu_plugin_runner_device_generic (self, device,
					      "fu_plugin_update_detach",
					      fu_plugin_device_detach,
					      error))
		return FALSE;

	/* run vfunc */
	g_debug ("verify(%s)", fu_plugin_get_name (self));
	if (!func (self, device, flags, &error_local)) {
		g_autoptr(GError) error_attach = NULL;
		if (error_local == NULL) {
			g_critical ("unset plugin error in verify(%s)",
				    fu_plugin_get_name (self));
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to verify using %s: ",
					    fu_plugin_get_name (self));
		/* make the device "work" again, but don't prefix the error */
		if (!fu_plugin_runner_device_generic (self, device,
						      "fu_plugin_update_attach",
						      fu_plugin_device_attach,
						      &error_attach)) {
			g_warning ("failed to attach whilst aborting verify(): %s",
				   error_attach->message);
		}
		return FALSE;
	}

	/* run optional attach */
	if (!fu_plugin_runner_device_generic (self, device,
					      "fu_plugin_update_attach",
					      fu_plugin_device_attach,
					      error))
		return FALSE;

	/* success */
	return TRUE;
}

/**
 * fu_plugin_runner_activate:
 * @self: a #FuPlugin
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Call into the plugin's activate routine
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.2.6
 **/
gboolean
fu_plugin_runner_activate (FuPlugin *self, FuDevice *device, GError **error)
{
	guint64 flags;

	g_return_val_if_fail (FU_IS_PLUGIN (self), FALSE);
	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

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
					      "fu_plugin_activate",
					      fu_plugin_device_activate,
					      error))
		return FALSE;

	/* update with correct flags */
	fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	fu_device_set_modified (device, (guint64) g_get_real_time () / G_USEC_PER_SEC);
	return TRUE;
}

/**
 * fu_plugin_runner_unlock:
 * @self: a #FuPlugin
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Call into the plugin's unlock routine
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 0.8.0
 **/
gboolean
fu_plugin_runner_unlock (FuPlugin *self, FuDevice *device, GError **error)
{
	guint64 flags;

	g_return_val_if_fail (FU_IS_PLUGIN (self), FALSE);
	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

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
					      "fu_plugin_unlock",
					      NULL,
					      error))
		return FALSE;

	/* update with correct flags */
	fu_device_remove_flag (device, FWUPD_DEVICE_FLAG_LOCKED);
	fu_device_set_modified (device, (guint64) g_get_real_time () / G_USEC_PER_SEC);
	return TRUE;
}

/**
 * fu_plugin_runner_update:
 * @self: a #FuPlugin
 * @device: a device
 * @blob_fw: a data blob
 * @flags: install flags
 * @error: (nullable): optional return location for an error
 *
 * Call into the plugin's update routine
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 0.8.0
 **/
gboolean
fu_plugin_runner_update (FuPlugin *self,
			 FuDevice *device,
			 GBytes *blob_fw,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginUpdateFunc update_func;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (FU_IS_PLUGIN (self), FALSE);
	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not enabled */
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_DISABLED)) {
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
		g_debug ("superclassed write_firmware(%s)", fu_plugin_get_name (self));
		return fu_plugin_device_write_firmware (self, device, blob_fw, flags, error);
	}

	/* online */
	if (!update_func (self, device, blob_fw, flags, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset plugin error in update(%s)",
				    fu_plugin_get_name (self));
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

	/* success */
	return TRUE;
}

/**
 * fu_plugin_runner_clear_results:
 * @self: a #FuPlugin
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Call into the plugin's clear results routine
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 0.8.0
 **/
gboolean
fu_plugin_runner_clear_results (FuPlugin *self, FuDevice *device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginDeviceFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (FU_IS_PLUGIN (self), FALSE);
	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not enabled */
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_clear_results", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("clear_result(%s)", fu_plugin_get_name (self));
	if (!func (self, device, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset plugin error in clear_result(%s)",
				    fu_plugin_get_name (self));
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to clear_result using %s: ",
					    fu_plugin_get_name (self));
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_plugin_runner_get_results:
 * @self: a #FuPlugin
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Call into the plugin's get results routine
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 0.8.0
 **/
gboolean
fu_plugin_runner_get_results (FuPlugin *self, FuDevice *device, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginDeviceFunc func = NULL;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail (FU_IS_PLUGIN (self), FALSE);
	g_return_val_if_fail (FU_IS_DEVICE (device), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* not enabled */
	if (fu_plugin_has_flag (self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* no object loaded */
	if (priv->module == NULL)
		return TRUE;

	/* optional */
	g_module_symbol (priv->module, "fu_plugin_get_results", (gpointer *) &func);
	if (func == NULL)
		return TRUE;
	g_debug ("get_results(%s)", fu_plugin_get_name (self));
	if (!func (self, device, &error_local)) {
		if (error_local == NULL) {
			g_critical ("unset plugin error in get_results(%s)",
				    fu_plugin_get_name (self));
			g_set_error_literal (&error_local,
					     FWUPD_ERROR,
					     FWUPD_ERROR_INTERNAL,
					     "unspecified error");
		}
		g_propagate_prefixed_error (error, g_steal_pointer (&error_local),
					    "failed to get_results using %s: ",
					    fu_plugin_get_name (self));
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
 *
 * Since: 1.0.0
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
 * @order: an integer value
 *
 * Sets the plugin order, where higher numbers are run after lower
 * numbers.
 *
 * Since: 1.0.0
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
 *
 * Since: 1.1.1
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
 * @priority: an integer value
 *
 * Sets the plugin priority, where higher numbers are better.
 *
 * Since: 1.0.0
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
 * @rule: a plugin rule, e.g. %FU_PLUGIN_RULE_CONFLICTS
 * @name: a plugin name, e.g. `upower`
 *
 * If the plugin name is found, the rule will be used to sort the plugin list,
 * for example the plugin specified by @name will be ordered after this plugin
 * when %FU_PLUGIN_RULE_RUN_AFTER is used.
 *
 * NOTE: the depsolver is iterative and may not solve overly-complicated rules;
 * If depsolving fails then fwupd will not start.
 *
 * Since: 1.0.0
 **/
void
fu_plugin_add_rule (FuPlugin *self, FuPluginRule rule, const gchar *name)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (self);
	if (priv->rules[rule] == NULL)
		priv->rules[rule] = g_ptr_array_new_with_free_func (g_free);
	g_ptr_array_add (priv->rules[rule], g_strdup (name));
	g_signal_emit (self, signals[SIGNAL_RULES_CHANGED], 0);
}

/**
 * fu_plugin_get_rules:
 * @self: a #FuPlugin
 * @rule: a plugin rule, e.g. %FU_PLUGIN_RULE_CONFLICTS
 *
 * Gets the plugin IDs that should be run after this plugin.
 *
 * Returns: (element-type utf8) (transfer none) (nullable): the list of plugin names, e.g. `['appstream']`
 *
 * Since: 1.0.0
 **/
GPtrArray *
fu_plugin_get_rules (FuPlugin *self, FuPluginRule rule)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (self);
	g_return_val_if_fail (rule < FU_PLUGIN_RULE_LAST, NULL);
	return priv->rules[rule];
}

/**
 * fu_plugin_has_rule:
 * @self: a #FuPlugin
 * @rule: a plugin rule, e.g. %FU_PLUGIN_RULE_CONFLICTS
 * @name: a plugin name, e.g. `upower`
 *
 * Gets the plugin IDs that should be run after this plugin.
 *
 * Returns: %TRUE if the name exists for the specific rule
 *
 * Since: 1.0.0
 **/
gboolean
fu_plugin_has_rule (FuPlugin *self, FuPluginRule rule, const gchar *name)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (self);
	if (priv->rules[rule] == NULL)
		return FALSE;
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
 *
 * Since: 1.0.4
 **/
void
fu_plugin_add_report_metadata (FuPlugin *self, const gchar *key, const gchar *value)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private (self);
	if (priv->report_metadata == NULL) {
		priv->report_metadata = g_hash_table_new_full (g_str_hash,
							       g_str_equal,
							       g_free,
							       g_free);
	}
	g_hash_table_insert (priv->report_metadata, g_strdup (key), g_strdup (value));
}

/**
 * fu_plugin_get_report_metadata:
 * @self: a #FuPlugin
 *
 * Returns the list of additional metadata to be added when filing a report.
 *
 * Returns: (transfer none) (nullable): the map of report metadata
 *
 * Since: 1.0.4
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
 * @key: a settings key
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
 * fu_plugin_get_config_value_boolean:
 * @self: a #FuPlugin
 * @key: a settings key
 *
 * Return the boolean value of a key if it's been configured
 *
 * Returns: %TRUE if the value is `true` (case insensitive), %FALSE otherwise
 *
 * Since: 1.4.0
 **/
gboolean
fu_plugin_get_config_value_boolean (FuPlugin *self, const gchar *key)
{
	g_autofree gchar *tmp = fu_plugin_get_config_value (self, key);
	if (tmp == NULL)
		return FALSE;
	return g_ascii_strcasecmp (tmp, "true") == 0;
}

/**
 * fu_plugin_name_compare:
 * @plugin1: first #FuPlugin to compare.
 * @plugin2: second #FuPlugin to compare.
 *
 * Compares two plugins by their names.
 *
 * Returns: 1, 0 or -1 if @plugin1 is greater, equal, or less than @plugin2.
 *
 * Since: 1.0.8
 **/
gint
fu_plugin_name_compare (FuPlugin *plugin1, FuPlugin *plugin2)
{
	return g_strcmp0 (fu_plugin_get_name (plugin1), fu_plugin_get_name (plugin2));
}

/**
 * fu_plugin_order_compare:
 * @plugin1: first #FuPlugin to compare.
 * @plugin2: second #FuPlugin to compare.
 *
 * Compares two plugins by their depsolved order.
 *
 * Returns: 1, 0 or -1 if @plugin1 is greater, equal, or less than @plugin2.
 *
 * Since: 1.0.8
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
	g_rw_lock_init (&priv->cache_mutex);
}

static void
fu_plugin_finalize (GObject *object)
{
	FuPlugin *self = FU_PLUGIN (object);
	FuPluginPrivate *priv = GET_PRIVATE (self);
	FuPluginInitFunc func = NULL;

	g_rw_lock_clear (&priv->cache_mutex);

	/* optional */
	if (priv->module != NULL) {
		g_module_symbol (priv->module, "fu_plugin_destroy", (gpointer *) &func);
		if (func != NULL) {
			g_debug ("destroy(%s)", fu_plugin_get_name (self));
			func (self);
		}
	}

	for (guint i = 0; i < FU_PLUGIN_RULE_LAST; i++) {
		if (priv->rules[i] != NULL)
			g_ptr_array_unref (priv->rules[i]);
	}
	if (priv->devices != NULL)
		g_ptr_array_unref (priv->devices);
	if (priv->ctx != NULL)
		g_object_unref (priv->ctx);
	if (priv->runtime_versions != NULL)
		g_hash_table_unref (priv->runtime_versions);
	if (priv->compile_versions != NULL)
		g_hash_table_unref (priv->compile_versions);
	if (priv->report_metadata != NULL)
		g_hash_table_unref (priv->report_metadata);
	if (priv->cache != NULL)
		g_hash_table_unref (priv->cache);
	if (priv->device_gtypes != NULL)
		g_array_unref (priv->device_gtypes);
	g_free (priv->build_hash);
	g_free (priv->data);

	G_OBJECT_CLASS (fu_plugin_parent_class)->finalize (object);
}

/**
 * fu_plugin_new:
 * @ctx: (nullable): a #FuContext
 *
 * Creates a new #FuPlugin
 *
 * Since: 0.8.0
 **/
FuPlugin *
fu_plugin_new (FuContext *ctx)
{
	FuPlugin *self = FU_PLUGIN (g_object_new (FU_TYPE_PLUGIN, NULL));
	FuPluginPrivate *priv = GET_PRIVATE (self);
	if (ctx != NULL)
		priv->ctx = g_object_ref (ctx);
	return self;
}

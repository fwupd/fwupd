/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuPlugin"

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <fwupd.h>
#include <gmodule.h>
#include <string.h>
#include <unistd.h>

#include "fu-bytes.h"
#include "fu-config-private.h"
#include "fu-context-private.h"
#include "fu-device-private.h"
#include "fu-kernel.h"
#include "fu-path.h"
#include "fu-plugin-private.h"
#include "fu-security-attr.h"
#include "fu-string.h"

/**
 * FuPlugin:
 *
 * A plugin which is used by fwupd to enumerate and update devices.
 *
 * See also: [class@FuDevice], [class@Fwupd.Plugin]
 */

static void
fu_plugin_finalize(GObject *object);

typedef struct {
	GModule *module;
	guint order;
	guint priority;
	gboolean done_init;
	GPtrArray *rules[FU_PLUGIN_RULE_LAST];
	GPtrArray *devices; /* (nullable) (element-type FuDevice) */
	GHashTable *runtime_versions;
	GHashTable *compile_versions;
	FuContext *ctx;
	GArray *device_gtypes; /* (nullable): of #GType */
	GType device_gtype_default;
	GHashTable *cache;	     /* (nullable): platform_id:GObject */
	GHashTable *report_metadata; /* (nullable): key:value */
	GFileMonitor *config_monitor;
	FuPluginData *data;
	FuPluginVfuncs vfuncs;
} FuPluginPrivate;

enum { PROP_0, PROP_CONTEXT, PROP_LAST };

enum {
	SIGNAL_DEVICE_ADDED,
	SIGNAL_DEVICE_REMOVED,
	SIGNAL_DEVICE_REGISTER,
	SIGNAL_RULES_CHANGED,
	SIGNAL_CHECK_SUPPORTED,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = {0};

G_DEFINE_TYPE_WITH_PRIVATE(FuPlugin, fu_plugin, FWUPD_TYPE_PLUGIN)
#define GET_PRIVATE(o) (fu_plugin_get_instance_private(o))

typedef void (*FuPluginInitVfuncsFunc)(FuPluginVfuncs *vfuncs);
typedef gboolean (*FuPluginDeviceFunc)(FuPlugin *self, FuDevice *device, GError **error);
typedef gboolean (*FuPluginDeviceProgressFunc)(FuPlugin *self,
					       FuDevice *device,
					       FuProgress *progress,
					       GError **error);
typedef gboolean (*FuPluginFlaggedDeviceFunc)(FuPlugin *self,
					      FuDevice *device,
					      FuProgress *progress,
					      FwupdInstallFlags flags,
					      GError **error);
typedef gboolean (*FuPluginDeviceArrayFunc)(FuPlugin *self, GPtrArray *devices, GError **error);

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
fu_plugin_is_open(FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);
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
fu_plugin_get_name(FuPlugin *self)
{
	g_return_val_if_fail(FU_IS_PLUGIN(self), NULL);
	return fwupd_plugin_get_name(FWUPD_PLUGIN(self));
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
fu_plugin_set_name(FuPlugin *self, const gchar *name)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FU_IS_PLUGIN(self));
	g_return_if_fail(!priv->done_init);

	if (g_strcmp0(name, fwupd_plugin_get_name(FWUPD_PLUGIN(self))) == 0) {
		g_critical("plugin name set to original value: %s", name);
		return;
	}
	if (fwupd_plugin_get_name(FWUPD_PLUGIN(self)) != NULL) {
		g_debug("overwriting plugin name %s -> %s",
			fwupd_plugin_get_name(FWUPD_PLUGIN(self)),
			name);
	}
	fwupd_plugin_set_name(FWUPD_PLUGIN(self), name);
}

static FuPluginVfuncs *
fu_plugin_get_vfuncs(FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_MODULAR))
		return &priv->vfuncs;
	return FU_PLUGIN_GET_CLASS(self);
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
fu_plugin_cache_lookup(FuPlugin *self, const gchar *id)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_PLUGIN(self), NULL);
	g_return_val_if_fail(id != NULL, NULL);
	if (priv->cache == NULL)
		return NULL;
	return g_hash_table_lookup(priv->cache, id);
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
fu_plugin_cache_add(FuPlugin *self, const gchar *id, gpointer dev)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PLUGIN(self));
	g_return_if_fail(id != NULL);
	g_return_if_fail(G_IS_OBJECT(dev));
	if (priv->cache == NULL) {
		priv->cache = g_hash_table_new_full(g_str_hash,
						    g_str_equal,
						    g_free,
						    (GDestroyNotify)g_object_unref);
	}
	g_hash_table_insert(priv->cache, g_strdup(id), g_object_ref(dev));
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
fu_plugin_cache_remove(FuPlugin *self, const gchar *id)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PLUGIN(self));
	g_return_if_fail(id != NULL);
	if (priv->cache == NULL)
		return;
	g_hash_table_remove(priv->cache, id);
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
fu_plugin_get_data(FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_PLUGIN(self), NULL);
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
fu_plugin_alloc_data(FuPlugin *self, gsize data_sz)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_PLUGIN(self), NULL);
	if (priv->data != NULL) {
		g_critical("fu_plugin_alloc_data() already used by plugin");
		return priv->data;
	}
	priv->data = g_malloc0(data_sz);
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
fu_plugin_guess_name_from_fn(const gchar *filename)
{
	const gchar *prefix = "libfu_plugin_";
	gchar *name;
	gchar *str = g_strstr_len(filename, -1, prefix);
	if (str == NULL)
		return NULL;
	name = g_strdup(str + strlen(prefix));
	g_strdelimit(name, ".", '\0');
	return name;
}

/**
 * fu_plugin_open:
 * @self: a #FuPlugin
 * @filename: the shared object filename to open
 * @error: (nullable): optional return location for an error
 *
 * Opens the plugin module, and calls `->load()` on it.
 *
 * Returns: TRUE for success, FALSE for fail
 *
 * Since: 0.8.0
 **/
gboolean
fu_plugin_open(FuPlugin *self, const gchar *filename, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);
	FuPluginVfuncs *vfuncs;
	FuPluginInitVfuncsFunc init_vfuncs = NULL;

	g_return_val_if_fail(FU_IS_PLUGIN(self), FALSE);
	g_return_val_if_fail(filename != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	priv->module = g_module_open(filename, 0);
	if (priv->module == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to open plugin %s: %s",
			    filename,
			    g_module_error());
		fu_plugin_add_flag(self, FWUPD_PLUGIN_FLAG_FAILED_OPEN);
		fu_plugin_add_flag(self, FWUPD_PLUGIN_FLAG_USER_WARNING);
		return FALSE;
	}

	/* call the vfunc setup */
	g_module_symbol(priv->module, "fu_plugin_init_vfuncs", (gpointer *)&init_vfuncs);
	if (init_vfuncs == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "failed to init_vfuncs() on plugin %s",
			    filename);
		fu_plugin_add_flag(self, FWUPD_PLUGIN_FLAG_FAILED_OPEN);
		fu_plugin_add_flag(self, FWUPD_PLUGIN_FLAG_USER_WARNING);
		return FALSE;
	}

	/* we can't "fallback" from modular to built-in so this is safe */
	fu_plugin_add_flag(self, FWUPD_PLUGIN_FLAG_MODULAR);
	vfuncs = fu_plugin_get_vfuncs(self);
	init_vfuncs(vfuncs);

	/* set automatically */
	if (fu_plugin_get_name(self) == NULL) {
		g_autofree gchar *str = fu_plugin_guess_name_from_fn(filename);
		fu_plugin_set_name(self, str);
	}

	/* optional */
	if (vfuncs->load != NULL) {
		FuContext *ctx = fu_plugin_get_context(self);
		g_debug("load(%s)", filename);
		vfuncs->load(ctx);
	}

	return TRUE;
}

/**
 * fu_plugin_add_string:
 * @self: a #FuPlugin
 * @idt: indent level
 * @str: a string to append to
 *
 * Add daemon-specific device metadata to an existing string.
 *
 * Since: 1.8.4
 **/
void
fu_plugin_add_string(FuPlugin *self, guint idt, GString *str)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);

	g_return_if_fail(FU_IS_PLUGIN(self));
	g_return_if_fail(str != NULL);

	/* attributes */
	fwupd_codec_add_string(FWUPD_CODEC(self), idt, str);
	fwupd_codec_string_append_int(str, idt + 1, "Order", priv->order);
	fwupd_codec_string_append_int(str, idt + 1, "Priority", priv->priority);
	if (priv->device_gtype_default != G_TYPE_INVALID) {
		fwupd_codec_string_append(str,
					  idt + 1,
					  "DeviceGTypeDefault",
					  g_type_name(priv->device_gtype_default));
	}

	/* optional */
	if (vfuncs->to_string != NULL)
		vfuncs->to_string(self, idt + 1, str);
}

/**
 * fu_plugin_to_string:
 * @self: a #FuPlugin
 *
 * This allows us to easily print the plugin metadata.
 *
 * Returns: a string value, or %NULL for invalid.
 *
 * Since: 1.8.4
 **/
gchar *
fu_plugin_to_string(FuPlugin *self)
{
	g_autoptr(GString) str = g_string_new(NULL);
	g_return_val_if_fail(FU_IS_PLUGIN(self), NULL);
	fu_plugin_add_string(self, 0, str);
	return g_string_free(g_steal_pointer(&str), FALSE);
}

/* order of usefulness to the user */
static const gchar *
fu_plugin_build_device_update_error(FuPlugin *self)
{
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_NO_HARDWARE))
		return "Not updatable as required hardware was not found";
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_LEGACY_BIOS))
		return "Not updatable in legacy BIOS mode";
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_CAPSULES_UNSUPPORTED))
		return "Not updatable as UEFI capsule updates not enabled in firmware setup";
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_UNLOCK_REQUIRED))
		return "Not updatable as requires unlock";
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_AUTH_REQUIRED))
		return "Not updatable as requires authentication";
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_EFIVAR_NOT_MOUNTED))
		return "Not updatable as efivarfs was not found";
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_ESP_NOT_FOUND))
		return "Not updatable as UEFI ESP partition not detected";
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return "Not updatable as plugin was disabled";
	return NULL;
}

static void
fu_plugin_ensure_devices(FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);
	if (priv->devices != NULL)
		return;
	priv->devices = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
}

static void
fu_plugin_device_child_added_cb(FuDevice *device, FuDevice *child, FuPlugin *self)
{
	g_debug("child %s added to parent %s after setup, adding to daemon",
		fu_device_get_id(child),
		fu_device_get_id(device));
	fu_plugin_device_add(self, child);
}

static void
fu_plugin_device_child_removed_cb(FuDevice *device, FuDevice *child, FuPlugin *self)
{
	g_debug("child %s removed from parent %s after setup, removing from daemon",
		fu_device_get_id(child),
		fu_device_get_id(device));
	fu_plugin_device_remove(self, child);
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
 * Since: 0.8.0
 **/
void
fu_plugin_device_add(FuPlugin *self, FuDevice *device)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);
	GPtrArray *children;
	g_autoptr(GError) error = NULL;

	g_return_if_fail(FU_IS_PLUGIN(self));
	g_return_if_fail(FU_IS_DEVICE(device));

	/* ensure the device ID is set from the physical and logical IDs */
	if (!fu_device_ensure_id(device, &error)) {
		g_warning("ignoring add: %s", error->message);
		return;
	}

	/* add to array */
	fu_plugin_ensure_devices(self);
	g_ptr_array_add(priv->devices, g_object_ref(device));

	/* proxy to device where required */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_CLEAR_UPDATABLE)) {
		if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_USER_WARNING)) {
			fu_device_inhibit(device,
					  "clear-updatable",
					  fu_plugin_build_device_update_error(self));
		} else {
			fu_device_inhibit(device,
					  "clear-updatable",
					  "Plugin disallowed updates with no user warning");
		}
	}

	g_debug("emit added from %s: %s", fu_plugin_get_name(self), fu_device_get_id(device));
	if (fu_device_get_created_usec(device) == 0)
		fu_device_set_created_usec(device, g_get_real_time());
	fu_device_set_plugin(device, fu_plugin_get_name(self));
	g_signal_emit(self, signals[SIGNAL_DEVICE_ADDED], 0, device);

	/* add children if they have not already been added */
	children = fu_device_get_children(device);
	for (guint i = 0; i < children->len; i++) {
		FuDevice *child = g_ptr_array_index(children, i);
		if (fu_device_get_created_usec(child) == 0)
			fu_plugin_device_add(self, child);
	}

	/* watch to see if children are added or removed at runtime */
	g_signal_connect(FU_DEVICE(device),
			 "child-added",
			 G_CALLBACK(fu_plugin_device_child_added_cb),
			 self);
	g_signal_connect(FU_DEVICE(device),
			 "child-removed",
			 G_CALLBACK(fu_plugin_device_child_removed_cb),
			 self);
}

/**
 * fu_plugin_get_devices:
 * @self: a #FuPlugin
 *
 * Returns all devices added by the plugin using [method@FuPlugin.device_add] and
 * not yet removed with [method@FuPlugin.device_remove].
 *
 * Returns: (transfer none) (element-type FuDevice): devices
 *
 * Since: 1.5.6
 **/
GPtrArray *
fu_plugin_get_devices(FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);
	g_return_val_if_fail(FU_IS_PLUGIN(self), NULL);
	fu_plugin_ensure_devices(self);
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
 * when using [method@FuPlugin.device_add]. They may wish to use this manually
 * if for instance the coldplug should be ignored based on the metadata
 * set from other plugins.
 *
 * Since: 0.9.7
 **/
void
fu_plugin_device_register(FuPlugin *self, FuDevice *device)
{
	g_autoptr(GError) error = NULL;

	g_return_if_fail(FU_IS_PLUGIN(self));
	g_return_if_fail(FU_IS_DEVICE(device));

	/* ensure the device ID is set from the physical and logical IDs */
	if (!fu_device_ensure_id(device, &error)) {
		g_warning("ignoring registration: %s", error->message);
		return;
	}

	g_debug("emit device-register from %s: %s",
		fu_plugin_get_name(self),
		fu_device_get_id(device));
	g_signal_emit(self, signals[SIGNAL_DEVICE_REGISTER], 0, device);
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
fu_plugin_device_remove(FuPlugin *self, FuDevice *device)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FU_IS_PLUGIN(self));
	g_return_if_fail(FU_IS_DEVICE(device));

	g_debug("emit removed from %s: %s", fu_plugin_get_name(self), fu_device_get_id(device));
	g_signal_emit(self, signals[SIGNAL_DEVICE_REMOVED], 0, device);

	/* remove from array */
	if (priv->devices != NULL)
		g_ptr_array_remove(priv->devices, device);
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
fu_plugin_check_supported(FuPlugin *self, const gchar *guid)
{
	gboolean retval = FALSE;
	g_signal_emit(self, signals[SIGNAL_CHECK_SUPPORTED], 0, guid, &retval);
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
fu_plugin_get_context(FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);
	return priv->ctx;
}

/**
 * fu_plugin_set_context:
 * @self: a #FuPlugin
 * @ctx: (nullable): optional #FuContext
 *
 * Sets the context for this plugin.
 *
 * Since: 1.8.6
 **/
void
fu_plugin_set_context(FuPlugin *self, FuContext *ctx)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_PLUGIN(self));
	g_return_if_fail(FU_IS_CONTEXT(ctx) || ctx == NULL);

	if (g_set_object(&priv->ctx, ctx))
		g_object_notify(G_OBJECT(self), "context");
}

static gboolean
fu_plugin_device_attach(FuPlugin *self, FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy_with_fallback(device);
	FuDeviceClass *device_class = FU_DEVICE_GET_CLASS(proxy);
	g_autoptr(FuDeviceLocker) locker = NULL;
	if (device_class->attach == NULL)
		return TRUE;
	locker = fu_device_locker_new(proxy, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_attach_full(device, progress, error);
}

static gboolean
fu_plugin_device_detach(FuPlugin *self, FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy_with_fallback(device);
	FuDeviceClass *device_class = FU_DEVICE_GET_CLASS(proxy);
	g_autoptr(FuDeviceLocker) locker = NULL;
	if (device_class->detach == NULL)
		return TRUE;
	locker = fu_device_locker_new(proxy, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_detach_full(device, progress, error);
}

static gboolean
fu_plugin_device_activate(FuPlugin *self, FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy_with_fallback(device);
	FuDeviceClass *device_class = FU_DEVICE_GET_CLASS(proxy);
	g_autoptr(FuDeviceLocker) locker = NULL;
	if (device_class->activate == NULL)
		return TRUE;
	locker = fu_device_locker_new(proxy, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_activate(device, progress, error);
}

static gboolean
fu_plugin_device_write_firmware(FuPlugin *self,
				FuDevice *device,
				GInputStream *stream,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuDevice *proxy = fu_device_get_proxy_with_fallback(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	locker = fu_device_locker_new(proxy, error);
	if (locker == NULL) {
		g_prefix_error(error, "failed to open device: ");
		return FALSE;
	}

	/* back the old firmware up to /var/lib/fwupd */
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_BACKUP_BEFORE_INSTALL)) {
		g_autoptr(GBytes) fw_old = NULL;
		g_autofree gchar *path = NULL;
		g_autofree gchar *fn = NULL;
		g_autofree gchar *localstatedir = NULL;

		/* progress */
		fu_progress_set_id(progress, G_STRLOC);
		fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_READ, 25, NULL);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 75, NULL);

		fw_old = fu_device_dump_firmware(device, fu_progress_get_child(progress), error);
		if (fw_old == NULL) {
			g_prefix_error(error, "failed to backup old firmware: ");
			return FALSE;
		}
		localstatedir = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
		fn = g_strdup_printf("%s.bin", fu_device_get_version(device));
		path = g_build_filename(
		    localstatedir,
		    "backup",
		    fu_device_get_id(device),
		    fu_device_get_serial(device) != NULL ? fu_device_get_serial(device) : "default",
		    fn,
		    NULL);
		fu_progress_step_done(progress);
		if (!fu_bytes_set_contents(path, fw_old, error))
			return FALSE;
		if (!fu_device_write_firmware(device,
					      stream,
					      fu_progress_get_child(progress),
					      flags,
					      error))
			return FALSE;
		fu_progress_step_done(progress);
		return TRUE;
	}

	return fu_device_write_firmware(device, stream, progress, flags, error);
}

static gboolean
fu_plugin_device_get_results(FuPlugin *self, FuDevice *device, GError **error)
{
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(GError) error_local = NULL;
	locker = fu_device_locker_new(device, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_device_get_results(device, &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED))
			return TRUE;
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_plugin_device_read_firmware(FuPlugin *self,
			       FuDevice *device,
			       FuProgress *progress,
			       GError **error)
{
	FuDevice *proxy = fu_device_get_proxy_with_fallback(device);
	g_autoptr(FuDeviceLocker) locker = NULL;
	g_autoptr(FuFirmware) firmware = NULL;
	g_autoptr(GBytes) fw = NULL;
	GChecksumType checksum_types[] = {G_CHECKSUM_SHA1, G_CHECKSUM_SHA256, 0};
	locker = fu_device_locker_new(proxy, error);
	if (locker == NULL)
		return FALSE;
	if (!fu_device_detach_full(device, progress, error))
		return FALSE;
	firmware = fu_device_read_firmware(device, progress, error);
	if (firmware == NULL) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_device_attach_full(device, progress, &error_local))
			g_debug("ignoring attach failure: %s", error_local->message);
		g_prefix_error(error, "failed to read firmware: ");
		return FALSE;
	}
	fw = fu_firmware_write(firmware, error);
	if (fw == NULL) {
		g_autoptr(GError) error_local = NULL;
		if (!fu_device_attach_full(device, progress, &error_local))
			g_debug("ignoring attach failure: %s", error_local->message);
		g_prefix_error(error, "failed to write firmware: ");
		return FALSE;
	}
	for (guint i = 0; checksum_types[i] != 0; i++) {
		g_autofree gchar *hash = NULL;
		hash = g_compute_checksum_for_bytes(checksum_types[i], fw);
		fu_device_add_checksum(device, hash);
	}
	return fu_device_attach_full(device, progress, error);
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
fu_plugin_runner_startup(FuPlugin *self, FuProgress *progress, GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_PLUGIN(self), FALSE);

	/* progress */
	fu_progress_set_name(progress, fu_plugin_get_name(self));

	/* be helpful for unit tests */
	fu_plugin_runner_init(self);

	/* not enabled */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* optional */
	if (vfuncs->startup != NULL) {
		g_debug("startup(%s)", fu_plugin_get_name(self));
		if (!vfuncs->startup(self, progress, &error_local)) {
			if (error_local == NULL) {
				g_critical("unset plugin error in startup(%s)",
					   fu_plugin_get_name(self));
				g_set_error_literal(&error_local,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INTERNAL,
						    "unspecified error");
			}
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to startup using %s: ",
						   fu_plugin_get_name(self));
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

/**
 * fu_plugin_runner_ready:
 * @self: a #FuPlugin
 * @error: (nullable): optional return location for an error
 *
 * Runs the ready routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.9.6
 **/
gboolean
fu_plugin_runner_ready(FuPlugin *self, FuProgress *progress, GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_PLUGIN(self), FALSE);

	/* progress */
	fu_progress_set_name(progress, fu_plugin_get_name(self));
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;
	fu_plugin_add_flag(self, FWUPD_PLUGIN_FLAG_READY);
	if (vfuncs->ready == NULL)
		return TRUE;

	/* optional */
	g_debug("ready(%s)", fu_plugin_get_name(self));
	if (!vfuncs->ready(self, progress, &error_local)) {
		if (error_local == NULL) {
			g_critical("unset plugin error in ready(%s)", fu_plugin_get_name(self));
			g_set_error_literal(&error_local,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "unspecified error");
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to ready using %s: ",
					   fu_plugin_get_name(self));
		return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_plugin_runner_init:
 * @self: a #FuPlugin
 *
 * Runs the constructed routine for the plugin, if enabled.
 *
 * Since: 1.8.1
 **/
void
fu_plugin_runner_init(FuPlugin *self)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	FuPluginPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FU_IS_PLUGIN(self));

	/* already done */
	if (priv->done_init)
		return;

	/* not enabled */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return;

	/* optional */
	if (vfuncs->constructed != NULL) {
		g_debug("constructed(%s)", fu_plugin_get_name(self));
		vfuncs->constructed(G_OBJECT(self));
		priv->done_init = TRUE;
	}
}

static gboolean
fu_plugin_runner_device_generic(FuPlugin *self,
				FuDevice *device,
				const gchar *symbol_name,
				FuPluginDeviceFunc device_func,
				GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* not enabled */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* optional */
	if (device_func == NULL)
		return TRUE;
	g_debug("%s(%s)", symbol_name + 10, fu_plugin_get_name(self));
	if (!device_func(self, device, &error_local)) {
		if (error_local == NULL) {
			g_critical("unset plugin error in %s(%s)",
				   fu_plugin_get_name(self),
				   symbol_name + 10);
			g_set_error_literal(&error_local,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "unspecified error");
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to %s using %s: ",
					   symbol_name + 10,
					   fu_plugin_get_name(self));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_plugin_runner_device_generic_progress(FuPlugin *self,
					 FuDevice *device,
					 FuProgress *progress,
					 const gchar *symbol_name,
					 FuPluginDeviceProgressFunc device_func,
					 GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* not enabled */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* optional */
	if (device_func == NULL)
		return TRUE;
	g_debug("%s(%s)", symbol_name + 10, fu_plugin_get_name(self));
	if (!device_func(self, device, progress, &error_local)) {
		if (error_local == NULL) {
			g_critical("unset plugin error in %s(%s)",
				   fu_plugin_get_name(self),
				   symbol_name + 10);
			g_set_error_literal(&error_local,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "unspecified error");
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to %s using %s: ",
					   symbol_name + 10,
					   fu_plugin_get_name(self));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_plugin_runner_flagged_device_generic(FuPlugin *self,
					FuDevice *device,
					FuProgress *progress,
					FwupdInstallFlags flags,
					const gchar *symbol_name,
					FuPluginFlaggedDeviceFunc func,
					GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* not enabled */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* optional */
	if (func == NULL)
		return TRUE;
	g_debug("%s(%s)", symbol_name + 10, fu_plugin_get_name(self));
	if (!func(self, device, progress, flags, &error_local)) {
		if (error_local == NULL) {
			g_critical("unset plugin error in %s(%s)",
				   fu_plugin_get_name(self),
				   symbol_name + 10);
			g_set_error_literal(&error_local,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "unspecified error");
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to %s using %s: ",
					   symbol_name + 10,
					   fu_plugin_get_name(self));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_plugin_runner_device_array_generic(FuPlugin *self,
				      GPtrArray *devices,
				      const gchar *symbol_name,
				      FuPluginDeviceArrayFunc func,
				      GError **error)
{
	g_autoptr(GError) error_local = NULL;

	/* not enabled */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* optional */
	if (func == NULL)
		return TRUE;
	g_debug("%s(%s)", symbol_name + 10, fu_plugin_get_name(self));
	if (!func(self, devices, &error_local)) {
		if (error_local == NULL) {
			g_critical("unset plugin error in for %s(%s)",
				   fu_plugin_get_name(self),
				   symbol_name + 10);
			g_set_error_literal(&error_local,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "unspecified error");
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to %s using %s: ",
					   symbol_name + 10,
					   fu_plugin_get_name(self));
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_plugin_runner_coldplug:
 * @self: a #FuPlugin
 * @progress: a #FuProgress
 * @error: (nullable): optional return location for an error
 *
 * Runs the coldplug routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 0.8.0
 **/
gboolean
fu_plugin_runner_coldplug(FuPlugin *self, FuProgress *progress, GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_PLUGIN(self), FALSE);

	/* progress */
	fu_progress_set_name(progress, fu_plugin_get_name(self));

	/* not enabled */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* no HwId */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_REQUIRE_HWID))
		return TRUE;

	/* optional */
	if (vfuncs->coldplug == NULL)
		return TRUE;
	g_debug("coldplug(%s)", fu_plugin_get_name(self));
	if (!vfuncs->coldplug(self, progress, &error_local)) {
		if (error_local == NULL) {
			g_critical("unset plugin error in coldplug(%s)", fu_plugin_get_name(self));
			g_set_error_literal(&error_local,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "unspecified error");
		}
		/* coldplug failed, but we might have already added devices to the daemon... */
		if (priv->devices != NULL) {
			for (guint i = 0; i < priv->devices->len; i++) {
				FuDevice *device = g_ptr_array_index(priv->devices, i);
				g_warning("removing device %s due to failed coldplug",
					  fu_device_get_id(device));
				fu_plugin_device_remove(self, device);
			}
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to coldplug using %s: ",
					   fu_plugin_get_name(self));
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
fu_plugin_runner_composite_prepare(FuPlugin *self, GPtrArray *devices, GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	return fu_plugin_runner_device_array_generic(self,
						     devices,
						     "fu_plugin_composite_prepare",
						     vfuncs->composite_prepare,
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
fu_plugin_runner_composite_cleanup(FuPlugin *self, GPtrArray *devices, GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	return fu_plugin_runner_device_array_generic(self,
						     devices,
						     "fu_plugin_composite_cleanup",
						     vfuncs->composite_cleanup,
						     error);
}

/**
 * fu_plugin_runner_prepare:
 * @self: a #FuPlugin
 * @device: a device
 * @progress: a #FuProgress
 * @flags: install flags
 * @error: (nullable): optional return location for an error
 *
 * Runs the update_prepare routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.7.0
 **/
gboolean
fu_plugin_runner_prepare(FuPlugin *self,
			 FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	return fu_plugin_runner_flagged_device_generic(self,
						       device,
						       progress,
						       flags,
						       "fu_plugin_prepare",
						       vfuncs->prepare,
						       error);
}

/**
 * fu_plugin_runner_cleanup:
 * @self: a #FuPlugin
 * @device: a device
 * @progress: a #FuProgress
 * @flags: install flags
 * @error: (nullable): optional return location for an error
 *
 * Runs the update_cleanup routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.7.0
 **/
gboolean
fu_plugin_runner_cleanup(FuPlugin *self,
			 FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	return fu_plugin_runner_flagged_device_generic(self,
						       device,
						       progress,
						       flags,
						       "fu_plugin_cleanup",
						       vfuncs->cleanup,
						       error);
}

/**
 * fu_plugin_runner_attach:
 * @self: a #FuPlugin
 * @device: a device
 * @progress: a #FuProgress
 * @error: (nullable): optional return location for an error
 *
 * Runs the update_attach routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.7.0
 **/
gboolean
fu_plugin_runner_attach(FuPlugin *self, FuDevice *device, FuProgress *progress, GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	return fu_plugin_runner_device_generic_progress(
	    self,
	    device,
	    progress,
	    "fu_plugin_attach",
	    vfuncs->attach != NULL ? vfuncs->attach : fu_plugin_device_attach,
	    error);
}

/**
 * fu_plugin_runner_detach:
 * @self: a #FuPlugin
 * @device: a device
 * @progress: a #FuProgress
 * @error: (nullable): optional return location for an error
 *
 * Runs the update_detach routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.7.0
 **/
gboolean
fu_plugin_runner_detach(FuPlugin *self, FuDevice *device, FuProgress *progress, GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	return fu_plugin_runner_device_generic_progress(
	    self,
	    device,
	    progress,
	    "fu_plugin_detach",
	    vfuncs->detach != NULL ? vfuncs->detach : fu_plugin_device_detach,
	    error);
}

/**
 * fu_plugin_runner_reload:
 * @self: a #FuPlugin
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Runs reload routine for a device
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.7.0
 **/
gboolean
fu_plugin_runner_reload(FuPlugin *self, FuDevice *device, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy_with_fallback(device);
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* not enabled */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* no object loaded */
	locker = fu_device_locker_new(proxy, error);
	if (locker == NULL)
		return FALSE;
	return fu_device_reload(device, error);
}

/**
 * fu_plugin_runner_reboot_cleanup:
 * @self: a #FuPlugin
 * @device: a device
 * @error: (nullable): optional return location for an error
 *
 * Performs cleanup actions after the reboot has been performed.
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.9.7
 **/
gboolean
fu_plugin_runner_reboot_cleanup(FuPlugin *self, FuDevice *device, GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);

	/* optional */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;
	if (vfuncs->reboot_cleanup == NULL)
		return TRUE;
	g_debug("reboot_cleanup(%s)", fu_plugin_get_name(self));
	return vfuncs->reboot_cleanup(self, device, error);
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
fu_plugin_runner_add_security_attrs(FuPlugin *self, FuSecurityAttrs *attrs)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);

	/* optional, but gets called even for disabled plugins */
	if (vfuncs->add_security_attrs == NULL)
		return;
	g_debug("add_security_attrs(%s)", fu_plugin_get_name(self));
	vfuncs->add_security_attrs(self, attrs);
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
 * See also: fu_plugin_set_device_gtype_default()
 *
 * Since: 1.6.0
 **/
void
fu_plugin_add_device_gtype(FuPlugin *self, GType device_gtype)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FU_IS_PLUGIN(self));

	/* create as required */
	if (priv->device_gtypes == NULL)
		priv->device_gtypes = g_array_new(FALSE, FALSE, sizeof(GType));

	/* check for duplicates */
	for (guint i = 0; i < priv->device_gtypes->len; i++) {
		GType device_gtype_tmp = g_array_index(priv->device_gtypes, GType, i);
		if (device_gtype == device_gtype_tmp)
			return;
	}

	/* ensure (to allow quirks to use it) then add */
	g_type_ensure(device_gtype);
	g_array_append_val(priv->device_gtypes, device_gtype);
}

/**
 * fu_plugin_get_device_gtype_default:
 * @self: a #FuPlugin
 *
 * Gets the default device #GType.
 *
 * If there is only one possible #GType added from fu_plugin_add_device_gtype() it will also be
 * returned here.
 *
 * Returns: a #GType, or %G_TYPE_INVALID on error
 *
 * Since: 1.9.14
 **/
GType
fu_plugin_get_device_gtype_default(FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_PLUGIN(self), G_TYPE_INVALID);

	if (priv->device_gtype_default != G_TYPE_INVALID)
		return priv->device_gtype_default;
	if (priv->device_gtypes->len == 1)
		return g_array_index(priv->device_gtypes, GType, 0);
	return G_TYPE_INVALID;
}

/**
 * fu_plugin_set_device_gtype_default:
 * @self: a #FuPlugin
 * @device_gtype: a #GType, e.g. `FU_TYPE_DEVICE`
 *
 * Sets the default device #GType.
 *
 * This will also add the device #GType using fu_plugin_add_device_gtype().
 *
 * Plugins can use this method only in fu_plugin_init()
 *
 * Since: 1.9.14
 **/
void
fu_plugin_set_device_gtype_default(FuPlugin *self, GType device_gtype)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FU_IS_PLUGIN(self));

	fu_plugin_add_device_gtype(self, device_gtype);
	priv->device_gtype_default = device_gtype;
}

static gchar *
fu_plugin_string_uncamelcase(const gchar *str)
{
	GString *tmp = g_string_new(NULL);
	for (guint i = 0; str[i] != '\0'; i++) {
		if (g_ascii_islower(str[i]) || g_ascii_isdigit(str[i])) {
			g_string_append_c(tmp, str[i]);
			continue;
		}
		if (i > 0)
			g_string_append_c(tmp, '-');
		g_string_append_c(tmp, g_ascii_tolower(str[i]));
	}
	return g_string_free(tmp, FALSE);
}

static gboolean
fu_plugin_check_amdgpu_dpaux(FuPlugin *self, GError **error)
{
#ifdef __linux__
	gsize bufsz = 0;
	g_autofree gchar *buf = NULL;
	g_auto(GStrv) lines = NULL;

	/* no module support in the kernel, we can't test for amdgpu module */
	if (!g_file_test("/proc/modules", G_FILE_TEST_EXISTS))
		return TRUE;
	if (!g_file_get_contents("/proc/modules", &buf, &bufsz, error))
		return FALSE;
	lines = g_strsplit(buf, "\n", -1);
	for (guint i = 0; lines[i] != NULL; i++) {
		if (g_str_has_prefix(lines[i], "amdgpu ")) {
			/* released 2019! */
			return fu_kernel_check_version("5.2.0", error);
		}
	}
#endif
	return TRUE;
}

/**
 * fu_plugin_add_device_udev_subsystem:
 * @self: a #FuPlugin
 * @subsystem: a subsystem name, e.g. `pciport`
 *
 * Add this plugin as a possible handler of devices with this udev subsystem.
 * Use fu_plugin_add_udev_subsystem() if you just want to ensure the subsystem is watched.
 *
 * Plugins can use this method only in fu_plugin_init()
 *
 * Since: 1.9.3
 **/
void
fu_plugin_add_device_udev_subsystem(FuPlugin *self, const gchar *subsystem)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FU_IS_PLUGIN(self));
	g_return_if_fail(subsystem != NULL);

	/* see https://github.com/fwupd/fwupd/issues/1121 for more details */
	if (g_strcmp0(subsystem, "drm_dp_aux_dev") == 0) {
		g_autoptr(GError) error = NULL;
		if (!fu_plugin_check_amdgpu_dpaux(self, &error)) {
			g_warning("failed to add subsystem: %s", error->message);
			fu_plugin_add_flag(self, FWUPD_PLUGIN_FLAG_DISABLED);
			fu_plugin_add_flag(self, FWUPD_PLUGIN_FLAG_KERNEL_TOO_OLD);
			return;
		}
	}

	/* proxy */
	fu_context_add_udev_subsystem(priv->ctx, subsystem, fu_plugin_get_name(self));
}

/**
 * fu_plugin_add_udev_subsystem:
 * @self: a #FuPlugin
 * @subsystem: a subsystem name, e.g. `pciport`
 *
 * Registers the udev subsystem to be watched by the daemon.
 *
 * Plugins can use this method only in fu_plugin_init()
 *
 * Since: 1.6.2
 **/
void
fu_plugin_add_udev_subsystem(FuPlugin *self, const gchar *subsystem)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);

	g_return_if_fail(FU_IS_PLUGIN(self));
	g_return_if_fail(subsystem != NULL);

	/* proxy */
	fu_context_add_udev_subsystem(priv->ctx, subsystem, NULL);
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
fu_plugin_add_firmware_gtype(FuPlugin *self, const gchar *id, GType gtype)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *id_safe = NULL;
	if (id != NULL) {
		id_safe = g_strdup(id);
	} else {
		g_autoptr(GString) str = g_string_new(g_type_name(gtype));
		if (g_str_has_prefix(str->str, "Fu"))
			g_string_erase(str, 0, 2);
		g_string_replace(str, "Firmware", "", 1);
		id_safe = fu_plugin_string_uncamelcase(str->str);
	}
	fu_context_add_firmware_gtype(priv->ctx, id_safe, gtype);
}

static gboolean
fu_plugin_check_supported_device(FuPlugin *self, FuDevice *device)
{
	GPtrArray *instance_ids = fu_device_get_instance_ids(device);
	for (guint i = 0; i < instance_ids->len; i++) {
		const gchar *instance_id = g_ptr_array_index(instance_ids, i);
		g_autofree gchar *guid = fwupd_guid_hash_string(instance_id);
		if (fu_plugin_check_supported(self, guid))
			return TRUE;
	}
	return FALSE;
}

static gboolean
fu_plugin_backend_device_added(FuPlugin *self,
			       FuDevice *device,
			       FuProgress *progress,
			       GError **error)
{
	FuDevice *proxy;
	FuPluginPrivate *priv = GET_PRIVATE(self);
	GType device_gtype = fu_device_get_specialized_gtype(FU_DEVICE(device));
	GType proxy_gtype = fu_device_get_proxy_gtype(FU_DEVICE(device));
	g_autoptr(FuDevice) dev = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_NO_PROFILE);
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 4, "created");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 48, "open");
	fu_progress_add_step(progress, FWUPD_STATUS_LOADING, 48, "add");

	/* fall back to plugin default */
	if (device_gtype == G_TYPE_INVALID)
		device_gtype = fu_plugin_get_device_gtype_default(self);
	if (device_gtype == G_TYPE_INVALID) {
		if (priv->device_gtypes->len > 1) {
			g_autoptr(GString) str = g_string_new(NULL);
			for (guint i = 0; i < priv->device_gtypes->len; i++) {
				device_gtype = g_array_index(priv->device_gtypes, GType, i);
				if (str->len > 0)
					g_string_append(str, ",");
				g_string_append(str, g_type_name(device_gtype));
			}
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "too many GTypes to choose a default, got: %s",
				    str->str);
			return FALSE;
		}
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "no possible device GTypes");
		return FALSE;
	}

	/* create new device and incorporate existing properties */
	dev = g_object_new(device_gtype, "context", priv->ctx, NULL);
	fu_device_incorporate(dev, FU_DEVICE(device), FU_DEVICE_INCORPORATE_FLAG_ALL);

	/* any proxy device to create too? */
	if (proxy_gtype != G_TYPE_INVALID) {
		g_autoptr(FuDevice) proxy_tmp =
		    g_object_new(proxy_gtype, "context", priv->ctx, NULL);
		fu_device_incorporate(proxy_tmp, device, FU_DEVICE_INCORPORATE_FLAG_ALL);
		fu_device_add_private_flag(dev, FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
		fu_device_set_proxy(dev, proxy_tmp);
	}

	/* notify plugins */
	if (!fu_plugin_runner_device_created(self, dev, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* there are a lot of different devices that match, but not all respond
	 * well to opening -- so limit some ones with issued updates */
	if (fu_device_has_private_flag(dev, FU_DEVICE_PRIVATE_FLAG_ONLY_SUPPORTED)) {
		if (!fu_device_probe(dev, error))
			return FALSE;
		fu_device_convert_instance_ids(dev);
		if (!fu_plugin_check_supported_device(self, dev)) {
			GPtrArray *guids = fu_device_get_guids(dev);
			g_autofree gchar *guids_str = fu_strjoin(",", guids);
			g_info("%s has no updates, so ignoring device", guids_str);
			fu_progress_finished(progress);
			return TRUE;
		}
	}

	/* open */
	proxy = fu_device_get_proxy(dev);
	if (proxy != NULL) {
		g_autoptr(FuDeviceLocker) locker_proxy = NULL;
		locker_proxy = fu_device_locker_new(proxy, error);
		if (locker_proxy == NULL)
			return FALSE;
		fu_device_incorporate(dev, proxy, FU_DEVICE_INCORPORATE_FLAG_ALL);
	}
	locker = fu_device_locker_new(dev, error);
	if (locker == NULL)
		return FALSE;
	fu_progress_step_done(progress);

	/* add */
	fu_plugin_device_add(self, dev);
	fu_plugin_runner_device_added(self, dev);
	fu_progress_step_done(progress);
	return TRUE;
}

/**
 * fu_plugin_runner_backend_device_added:
 * @self: a #FuPlugin
 * @device: a device
 * @progress: a #FuProgress
 * @error: (nullable): optional return location for an error
 *
 * Call the backend_device_added routine for the plugin
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.5.6
 **/
gboolean
fu_plugin_runner_backend_device_added(FuPlugin *self,
				      FuDevice *device,
				      FuProgress *progress,
				      GError **error)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_PLUGIN(self), FALSE);
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(FU_IS_PROGRESS(progress), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* not enabled */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* optional */
	if (vfuncs->backend_device_added == NULL) {
		if (priv->device_gtypes != NULL ||
		    fu_device_get_specialized_gtype(device) != G_TYPE_INVALID) {
			return fu_plugin_backend_device_added(self, device, progress, error);
		}
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "No device GType set");
		return FALSE;
	}
	g_debug("backend_device_added(%s)", fu_plugin_get_name(self));
	if (!vfuncs->backend_device_added(self, device, progress, &error_local)) {
		if (error_local == NULL) {
			g_critical("unset plugin error in backend_device_added(%s)",
				   fu_plugin_get_name(self));
			g_set_error_literal(&error_local,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "unspecified error");
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to add device using on %s: ",
					   fu_plugin_get_name(self));
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
fu_plugin_runner_backend_device_changed(FuPlugin *self, FuDevice *device, GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_PLUGIN(self), FALSE);
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* not enabled */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* optional */
	if (vfuncs->backend_device_changed == NULL)
		return TRUE;
	g_debug("udev_device_changed(%s)", fu_plugin_get_name(self));
	if (!vfuncs->backend_device_changed(self, device, &error_local)) {
		if (error_local == NULL) {
			g_critical("unset plugin error in udev_device_changed(%s)",
				   fu_plugin_get_name(self));
			g_set_error_literal(&error_local,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "unspecified error");
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to change device on %s: ",
					   fu_plugin_get_name(self));
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
fu_plugin_runner_device_added(FuPlugin *self, FuDevice *device)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);

	/* not enabled */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return;

	/* optional */
	if (vfuncs->device_added == NULL)
		return;
	g_debug("fu_plugin_device_added(%s)", fu_plugin_get_name(self));
	vfuncs->device_added(self, device);
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
fu_plugin_runner_device_removed(FuPlugin *self, FuDevice *device)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	g_autoptr(GError) error_local = NULL;

	if (!fu_plugin_runner_device_generic(self,
					     device,
					     "fu_plugin_backend_device_removed",
					     vfuncs->backend_device_removed,
					     &error_local))
		g_warning("%s", error_local->message);
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
fu_plugin_runner_device_register(FuPlugin *self, FuDevice *device)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);

	/* not enabled */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return;

	/* optional */
	if (vfuncs->device_registered != NULL) {
		g_debug("fu_plugin_device_registered(%s)", fu_plugin_get_name(self));
		vfuncs->device_registered(self, device);
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
fu_plugin_runner_device_created(FuPlugin *self, FuDevice *device, GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);

	g_return_val_if_fail(FU_IS_PLUGIN(self), FALSE);
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* not enabled */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* optional */
	if (vfuncs->device_created == NULL)
		return TRUE;
	g_debug("fu_plugin_device_created(%s)", fu_plugin_get_name(self));
	return vfuncs->device_created(self, device, error);
}

/**
 * fu_plugin_runner_verify:
 * @self: a #FuPlugin
 * @device: a device
 * @progress: a #FuProgress
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
fu_plugin_runner_verify(FuPlugin *self,
			FuDevice *device,
			FuProgress *progress,
			FuPluginVerifyFlags flags,
			GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	GPtrArray *checksums;
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_PLUGIN(self), FALSE);
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(FU_IS_PROGRESS(progress), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* not enabled */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* optional */
	if (vfuncs->verify == NULL) {
		if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_CAN_VERIFY)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "device %s does not support verification",
				    fu_device_get_id(device));
			return FALSE;
		}
		return fu_plugin_device_read_firmware(self, device, progress, error);
	}

	/* clear any existing verification checksums */
	checksums = fu_device_get_checksums(device);
	g_ptr_array_set_size(checksums, 0);

	/* run additional detach */
	if (!fu_plugin_runner_device_generic_progress(
		self,
		device,
		progress,
		"fu_plugin_detach",
		vfuncs->detach != NULL ? vfuncs->detach : fu_plugin_device_detach,
		error))
		return FALSE;

	/* run vfunc */
	g_debug("verify(%s)", fu_plugin_get_name(self));
	if (!vfuncs->verify(self, device, progress, flags, &error_local)) {
		g_autoptr(GError) error_attach = NULL;
		if (error_local == NULL) {
			g_critical("unset plugin error in verify(%s)", fu_plugin_get_name(self));
			g_set_error_literal(&error_local,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "unspecified error");
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to verify using %s: ",
					   fu_plugin_get_name(self));
		/* make the device "work" again, but don't prefix the error */
		if (!fu_plugin_runner_device_generic_progress(
			self,
			device,
			progress,
			"fu_plugin_attach",
			vfuncs->attach != NULL ? vfuncs->attach : fu_plugin_device_attach,
			&error_attach)) {
			g_warning("failed to attach whilst aborting verify(): %s",
				  error_attach->message);
		}
		return FALSE;
	}

	/* run optional attach */
	if (!fu_plugin_runner_device_generic_progress(
		self,
		device,
		progress,
		"fu_plugin_attach",
		vfuncs->attach != NULL ? vfuncs->attach : fu_plugin_device_attach,
		error))
		return FALSE;

	/* success */
	return TRUE;
}

/**
 * fu_plugin_runner_activate:
 * @self: a #FuPlugin
 * @device: a device
 * @progress: a #FuProgress
 * @error: (nullable): optional return location for an error
 *
 * Call into the plugin's activate routine
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.2.6
 **/
gboolean
fu_plugin_runner_activate(FuPlugin *self, FuDevice *device, FuProgress *progress, GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	guint64 flags;

	g_return_val_if_fail(FU_IS_PLUGIN(self), FALSE);
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(FU_IS_PROGRESS(progress), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* final check */
	flags = fu_device_get_flags(device);
	if ((flags & FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Device %s does not need activation",
			    fu_device_get_id(device));
		return FALSE;
	}

	/* run vfunc */
	if (!fu_plugin_runner_device_generic_progress(
		self,
		device,
		progress,
		"fu_plugin_activate",
		vfuncs->activate != NULL ? vfuncs->activate : fu_plugin_device_activate,
		error))
		return FALSE;

	/* update with correct flags */
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_NEEDS_ACTIVATION);
	fu_device_set_modified_usec(device, g_get_real_time());
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
fu_plugin_runner_unlock(FuPlugin *self, FuDevice *device, GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	guint64 flags;

	g_return_val_if_fail(FU_IS_PLUGIN(self), FALSE);
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* final check */
	flags = fu_device_get_flags(device);
	if ((flags & FWUPD_DEVICE_FLAG_LOCKED) == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "Device %s is not locked",
			    fu_device_get_id(device));
		return FALSE;
	}

	/* run vfunc */
	if (!fu_plugin_runner_device_generic(self,
					     device,
					     "fu_plugin_unlock",
					     vfuncs->unlock,
					     error))
		return FALSE;

	/* update with correct flags */
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_LOCKED);
	fu_device_set_modified_usec(device, g_get_real_time());
	return TRUE;
}

/**
 * fu_plugin_runner_write_firmware:
 * @self: a #FuPlugin
 * @device: a device
 * @stream: a #GInputStream
 * @progress: a #FuProgress
 * @flags: install flags
 * @error: (nullable): optional return location for an error
 *
 * Call into the plugin's write firmware routine
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.7.0
 **/
gboolean
fu_plugin_runner_write_firmware(FuPlugin *self,
				FuDevice *device,
				GInputStream *stream,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_PLUGIN(self), FALSE);
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(G_IS_INPUT_STREAM(stream), FALSE);
	g_return_val_if_fail(FU_IS_PROGRESS(progress), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* not enabled */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED)) {
		g_debug("plugin not enabled, skipping");
		return TRUE;
	}

	/* optional */
	if (vfuncs->write_firmware != NULL) {
		if (!vfuncs->write_firmware(self, device, stream, progress, flags, &error_local)) {
			if (error_local == NULL) {
				g_critical("unset plugin error in update(%s)",
					   fu_plugin_get_name(self));
				g_set_error_literal(&error_local,
						    FWUPD_ERROR,
						    FWUPD_ERROR_INTERNAL,
						    "unspecified error");
				return FALSE;
			}
			fu_device_set_update_error(device, error_local->message);
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	} else {
		g_debug("superclassed write_firmware(%s)", fu_plugin_get_name(self));
		return fu_plugin_device_write_firmware(self,
						       device,
						       stream,
						       progress,
						       flags,
						       error);
	}

	/* no longer valid */
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_REBOOT) &&
	    !fu_device_has_flag(device, FWUPD_DEVICE_FLAG_NEEDS_SHUTDOWN)) {
		GPtrArray *checksums = fu_device_get_checksums(device);
		g_ptr_array_set_size(checksums, 0);
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
fu_plugin_runner_clear_results(FuPlugin *self, FuDevice *device, GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_PLUGIN(self), FALSE);
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* not enabled */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* optional */
	if (vfuncs->clear_results == NULL)
		return TRUE;
	g_debug("clear_result(%s)", fu_plugin_get_name(self));
	if (!vfuncs->clear_results(self, device, &error_local)) {
		if (error_local == NULL) {
			g_critical("unset plugin error in clear_result(%s)",
				   fu_plugin_get_name(self));
			g_set_error_literal(&error_local,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "unspecified error");
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to clear_result using %s: ",
					   fu_plugin_get_name(self));
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
fu_plugin_runner_get_results(FuPlugin *self, FuDevice *device, GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);
	g_autoptr(GError) error_local = NULL;

	g_return_val_if_fail(FU_IS_PLUGIN(self), FALSE);
	g_return_val_if_fail(FU_IS_DEVICE(device), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* not enabled */
	if (fu_plugin_has_flag(self, FWUPD_PLUGIN_FLAG_DISABLED))
		return TRUE;

	/* optional */
	if (vfuncs->get_results == NULL) {
		g_debug("superclassed get_results(%s)", fu_plugin_get_name(self));
		return fu_plugin_device_get_results(self, device, error);
	}
	g_debug("get_results(%s)", fu_plugin_get_name(self));
	if (!vfuncs->get_results(self, device, &error_local)) {
		if (error_local == NULL) {
			g_critical("unset plugin error in get_results(%s)",
				   fu_plugin_get_name(self));
			g_set_error_literal(&error_local,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "unspecified error");
		}
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to get_results using %s: ",
					   fu_plugin_get_name(self));
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_plugin_runner_fix_host_security_attr:
 * @self: a #FuPlugin
 * @attr: (nullable): a security attribute
 * @error: (nullable): optional return location for an error
 *
 * Fix the specific security attribute.
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.9.6
 **/
gboolean
fu_plugin_runner_fix_host_security_attr(FuPlugin *self, FwupdSecurityAttr *attr, GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);

	g_return_val_if_fail(FU_IS_PLUGIN(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (vfuncs->fix_host_security_attr == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "fix is not supported");
		return FALSE;
	}
	return vfuncs->fix_host_security_attr(self, attr, error);
}

/**
 * fu_plugin_runner_undo_host_security_attr:
 * @self: a #FuPlugin
 * @attr: (nullable): a security attribute
 * @error: (nullable): optional return location for an error
 *
 * Fix the security issue of given security attribute.
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 1.9.6
 **/
gboolean
fu_plugin_runner_undo_host_security_attr(FuPlugin *self, FwupdSecurityAttr *attr, GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);

	g_return_val_if_fail(FU_IS_PLUGIN(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (vfuncs->undo_host_security_attr == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "undo is not supported");
		return FALSE;
	}
	return vfuncs->undo_host_security_attr(self, attr, error);
}

/**
 * fu_plugin_runner_modify_config:
 * @self: a #FuPlugin
 * @key: a config key
 * @value: a config value
 * @error: (nullable): optional return location for an error
 *
 * Sets a plugin config option, which may be allow-listed or value-checked.
 *
 * Returns: #TRUE for success, #FALSE for failure
 *
 * Since: 2.0.0
 **/
gboolean
fu_plugin_runner_modify_config(FuPlugin *self, const gchar *key, const gchar *value, GError **error)
{
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);

	/* optional */
	if (vfuncs->modify_config == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot modify %s=%s config",
			    key,
			    value);
		return FALSE;
	}
	g_debug("modify_config(%s)", fu_plugin_get_name(self));
	return vfuncs->modify_config(self, key, value, error);
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
fu_plugin_get_order(FuPlugin *self)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private(self);
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
fu_plugin_set_order(FuPlugin *self, guint order)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private(self);
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
fu_plugin_get_priority(FuPlugin *self)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private(self);
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
fu_plugin_set_priority(FuPlugin *self, guint priority)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private(self);
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
 * NOTE: The depsolver is iterative and may not solve overly-complicated rules;
 * If depsolving fails then fwupd will not start.
 *
 * Since: 1.0.0
 **/
void
fu_plugin_add_rule(FuPlugin *self, FuPluginRule rule, const gchar *name)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private(self);
	if (priv->rules[rule] == NULL)
		priv->rules[rule] = g_ptr_array_new_with_free_func(g_free);
	g_ptr_array_add(priv->rules[rule], g_strdup(name));
	g_signal_emit(self, signals[SIGNAL_RULES_CHANGED], 0);
}

/**
 * fu_plugin_get_rules:
 * @self: a #FuPlugin
 * @rule: a plugin rule, e.g. %FU_PLUGIN_RULE_CONFLICTS
 *
 * Gets the plugin IDs that should be run after this plugin.
 *
 * Returns: (element-type utf8) (transfer none) (nullable): the list of plugin names, e.g.
 *`['appstream']`
 *
 * Since: 1.0.0
 **/
GPtrArray *
fu_plugin_get_rules(FuPlugin *self, FuPluginRule rule)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private(self);
	g_return_val_if_fail(rule < FU_PLUGIN_RULE_LAST, NULL);
	return priv->rules[rule];
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
fu_plugin_add_report_metadata(FuPlugin *self, const gchar *key, const gchar *value)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private(self);
	if (priv->report_metadata == NULL) {
		priv->report_metadata =
		    g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	}
	g_hash_table_insert(priv->report_metadata, g_strdup(key), g_strdup(value));
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
fu_plugin_get_report_metadata(FuPlugin *self)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private(self);
	return priv->report_metadata;
}

/**
 * fu_plugin_get_config_value:
 * @self: a #FuPlugin
 * @key: a settings key
 *
 * Return the value of a key, falling back to the default value.
 *
 * Since: 1.0.6
 **/
gchar *
fu_plugin_get_config_value(FuPlugin *self, const gchar *key)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private(self);
	FuConfig *config = fu_context_get_config(priv->ctx);
	const gchar *name;

	g_return_val_if_fail(FU_IS_PLUGIN(self), NULL);
	g_return_val_if_fail(key != NULL, NULL);

	if (config == NULL) {
		g_critical("cannot get config value with no loaded context!");
		return NULL;
	}
	name = fu_plugin_get_name(self);
	if (name == NULL) {
		g_critical("cannot get config value with no plugin name!");
		return NULL;
	}
	return fu_config_get_value(config, name, key);
}

/**
 * fu_plugin_set_config_default:
 * @self: a #FuPlugin
 * @key: a settings key
 * @value: the default value of the key if not found
 *
 * Sets the config default value.
 *
 * Since: 2.0.0
 **/
void
fu_plugin_set_config_default(FuPlugin *self, const gchar *key, const gchar *value)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private(self);
	FuConfig *config = fu_context_get_config(priv->ctx);
	const gchar *name;

	g_return_if_fail(FU_IS_PLUGIN(self));
	g_return_if_fail(key != NULL);

	if (config == NULL) {
		g_critical("cannot set config default with no loaded context!");
		return;
	}
	name = fu_plugin_get_name(self);
	if (name == NULL) {
		g_critical("cannot set config default with no plugin name!");
		return;
	}
	fu_config_set_default(config, name, key, value);
}

/**
 * fu_plugin_security_attr_new:
 * @self: a #FuPlugin
 * @appstream_id: (nullable): the AppStream component ID, e.g. `com.intel.BiosGuard`
 *
 * Creates a new #FwupdSecurityAttr for this specific plugin.
 *
 * Returns: (transfer full): a #FwupdSecurityAttr
 *
 * Since: 1.8.4
 **/
FwupdSecurityAttr *
fu_plugin_security_attr_new(FuPlugin *self, const gchar *appstream_id)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private(self);
	g_autoptr(FwupdSecurityAttr) attr = NULL;

	g_return_val_if_fail(FU_IS_PLUGIN(self), NULL);
	g_return_val_if_fail(appstream_id != NULL, NULL);

	attr = fu_security_attr_new(priv->ctx, appstream_id);
	fwupd_security_attr_set_plugin(attr, fu_plugin_get_name(self));
	return g_steal_pointer(&attr);
}

/**
 * fu_plugin_set_config_value:
 * @self: a #FuPlugin
 * @key: a settings key
 * @value: (nullable): a settings value
 * @error: (nullable): optional return location for an error
 *
 * Sets a plugin config value.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.7.0
 **/
gboolean
fu_plugin_set_config_value(FuPlugin *self, const gchar *key, const gchar *value, GError **error)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private(self);
	FuConfig *config = fu_context_get_config(priv->ctx);
	const gchar *name;

	g_return_val_if_fail(FU_IS_PLUGIN(self), FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (config == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "cannot get config value with no loaded context");
		return FALSE;
	}
	name = fu_plugin_get_name(self);
	if (name == NULL) {
		g_critical("cannot get config value with no plugin name!");
		return FALSE;
	}
	return fu_config_set_value(config, name, key, value, error);
}

/**
 * fu_plugin_reset_config_values:
 * @self: a #FuPlugin
 * @error: (nullable): optional return location for an error
 *
 * Reset all the plugin keys back to the default values.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.15
 **/
gboolean
fu_plugin_reset_config_values(FuPlugin *self, GError **error)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private(self);
	FuConfig *config = fu_context_get_config(priv->ctx);
	const gchar *name;

	g_return_val_if_fail(FU_IS_PLUGIN(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (config == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "cannot reset config values with no loaded context");
		return FALSE;
	}
	name = fu_plugin_get_name(self);
	if (name == NULL) {
		g_critical("cannot reset config values with no plugin name!");
		return FALSE;
	}
	return fu_config_reset_defaults(config, name, error);
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
fu_plugin_get_config_value_boolean(FuPlugin *self, const gchar *key)
{
	FuPluginPrivate *priv = fu_plugin_get_instance_private(self);
	FuConfig *config = fu_context_get_config(priv->ctx);
	const gchar *name;

	g_return_val_if_fail(FU_IS_PLUGIN(self), FALSE);
	g_return_val_if_fail(key != NULL, FALSE);

	if (config == NULL) {
		g_critical("cannot get config value with no loaded context!");
		return FALSE;
	}
	name = fu_plugin_get_name(self);
	if (name == NULL) {
		g_critical("cannot get config value with no plugin name!");
		return FALSE;
	}
	return fu_config_get_value_bool(config, name, key);
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
fu_plugin_name_compare(FuPlugin *plugin1, FuPlugin *plugin2)
{
	return g_strcmp0(fu_plugin_get_name(plugin1), fu_plugin_get_name(plugin2));
}

/**
 * fu_plugin_order_compare:
 * @plugin1: first #FuPlugin to compare.
 * @plugin2: second #FuPlugin to compare.
 *
 * Compares two plugins by their depsolved order, and then by name.
 *
 * Returns: 1, 0 or -1 if @plugin1 is greater, equal, or less than @plugin2.
 *
 * Since: 1.0.8
 **/
gint
fu_plugin_order_compare(FuPlugin *plugin1, FuPlugin *plugin2)
{
	FuPluginPrivate *priv1 = fu_plugin_get_instance_private(plugin1);
	FuPluginPrivate *priv2 = fu_plugin_get_instance_private(plugin2);
	if (priv1->order < priv2->order)
		return -1;
	if (priv1->order > priv2->order)
		return 1;
	return fu_plugin_name_compare(plugin1, plugin2);
}

static gchar *
fu_plugin_convert_gtype_to_name(GType gtype)
{
	const gchar *gtype_name = g_type_name(gtype);
	gsize len = strlen(gtype_name);
	g_autoptr(GString) str = g_string_new(NULL);

	g_return_val_if_fail(g_str_has_prefix(gtype_name, "Fu"), NULL);
	g_return_val_if_fail(g_str_has_suffix(gtype_name, "Plugin"), NULL);

	/* self tests */
	if (g_strcmp0(gtype_name, "FuPlugin") == 0)
		return g_strdup("plugin");

	/* normal plugins */
	for (guint j = 2; j < len - 6; j++) {
		gchar tmp = gtype_name[j];
		if (g_ascii_isupper(tmp)) {
			if (str->len > 0)
				g_string_append_c(str, '_');
			g_string_append_c(str, g_ascii_tolower(tmp));
		} else {
			g_string_append_c(str, tmp);
		}
	}
	if (str->len == 0)
		return NULL;
	return g_string_free(g_steal_pointer(&str), FALSE);
}

static void
fu_plugin_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuPlugin *self = FU_PLUGIN(object);
	FuPluginPrivate *priv = GET_PRIVATE(self);
	switch (prop_id) {
	case PROP_CONTEXT:
		g_value_set_object(value, priv->ctx);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_plugin_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuPlugin *self = FU_PLUGIN(object);
	switch (prop_id) {
	case PROP_CONTEXT:
		fu_plugin_set_context(self, g_value_get_object(value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_plugin_dispose(GObject *object)
{
	FuPlugin *self = FU_PLUGIN(object);
	FuPluginPrivate *priv = GET_PRIVATE(self);

	if (priv->devices != NULL)
		g_ptr_array_set_size(priv->devices, 0);
	if (priv->cache != NULL)
		g_hash_table_remove_all(priv->cache);
	g_clear_object(&priv->ctx);

	G_OBJECT_CLASS(fu_plugin_parent_class)->dispose(object);
}

static void
fu_plugin_class_init(FuPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->finalize = fu_plugin_finalize;
	object_class->dispose = fu_plugin_dispose;
	object_class->get_property = fu_plugin_get_property;
	object_class->set_property = fu_plugin_set_property;

	/**
	 * FuPlugin::device-added:
	 * @self: the #FuPlugin instance that emitted the signal
	 * @device: the #FuDevice
	 *
	 * The ::device-added signal is emitted when a device has been added by the plugin.
	 *
	 * Since: 0.8.0
	 **/
	signals[SIGNAL_DEVICE_ADDED] = g_signal_new("device-added",
						    G_TYPE_FROM_CLASS(object_class),
						    G_SIGNAL_RUN_LAST,
						    G_STRUCT_OFFSET(FuPluginClass, _device_added),
						    NULL,
						    NULL,
						    g_cclosure_marshal_VOID__OBJECT,
						    G_TYPE_NONE,
						    1,
						    FU_TYPE_DEVICE);
	/**
	 * FuPlugin::device-removed:
	 * @self: the #FuPlugin instance that emitted the signal
	 * @device: the #FuDevice
	 *
	 * The ::device-removed signal is emitted when a device has been removed by the plugin.
	 *
	 * Since: 0.8.0
	 **/
	signals[SIGNAL_DEVICE_REMOVED] =
	    g_signal_new("device-removed",
			 G_TYPE_FROM_CLASS(object_class),
			 G_SIGNAL_RUN_LAST,
			 G_STRUCT_OFFSET(FuPluginClass, _device_removed),
			 NULL,
			 NULL,
			 g_cclosure_marshal_VOID__OBJECT,
			 G_TYPE_NONE,
			 1,
			 FU_TYPE_DEVICE);
	/**
	 * FuPlugin::device-register:
	 * @self: the #FuPlugin instance that emitted the signal
	 * @device: the #FuDevice
	 *
	 * The ::device-register signal is emitted when another plugin has added the device.
	 *
	 * Since: 0.9.7
	 **/
	signals[SIGNAL_DEVICE_REGISTER] =
	    g_signal_new("device-register",
			 G_TYPE_FROM_CLASS(object_class),
			 G_SIGNAL_RUN_LAST,
			 G_STRUCT_OFFSET(FuPluginClass, _device_register),
			 NULL,
			 NULL,
			 g_cclosure_marshal_VOID__OBJECT,
			 G_TYPE_NONE,
			 1,
			 FU_TYPE_DEVICE);
	/**
	 * FuPlugin::check-supported:
	 * @self: the #FuPlugin instance that emitted the signal
	 * @guid: a device GUID
	 *
	 * The ::check-supported signal is emitted when a plugin wants to ask the daemon if a
	 * specific device GUID is supported in the existing system metadata.
	 *
	 * Returns: %TRUE if the GUID is found
	 *
	 * Since: 1.0.0
	 **/
	signals[SIGNAL_CHECK_SUPPORTED] =
	    g_signal_new("check-supported",
			 G_TYPE_FROM_CLASS(object_class),
			 G_SIGNAL_RUN_LAST,
			 G_STRUCT_OFFSET(FuPluginClass, _check_supported),
			 NULL,
			 NULL,
			 g_cclosure_marshal_generic,
			 G_TYPE_BOOLEAN,
			 1,
			 G_TYPE_STRING);
	signals[SIGNAL_RULES_CHANGED] = g_signal_new("rules-changed",
						     G_TYPE_FROM_CLASS(object_class),
						     G_SIGNAL_RUN_LAST,
						     G_STRUCT_OFFSET(FuPluginClass, _rules_changed),
						     NULL,
						     NULL,
						     g_cclosure_marshal_VOID__VOID,
						     G_TYPE_NONE,
						     0);

	/**
	 * FuPlugin:context:
	 *
	 * The #FuContext to use.
	 *
	 * Since: 1.8.6
	 */
	pspec = g_param_spec_object("context",
				    NULL,
				    NULL,
				    FU_TYPE_CONTEXT,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_CONTEXT, pspec);
}

static void
fu_plugin_init(FuPlugin *self)
{
	FuPluginPrivate *priv = GET_PRIVATE(self);
	priv->device_gtype_default = G_TYPE_INVALID;
}

static void
fu_plugin_finalize(GObject *object)
{
	FuPlugin *self = FU_PLUGIN(object);
	FuPluginPrivate *priv = GET_PRIVATE(self);
	FuPluginVfuncs *vfuncs = fu_plugin_get_vfuncs(self);

	/* optional */
	if (priv->done_init && vfuncs->finalize != NULL) {
		g_debug("finalize(%s)", fu_plugin_get_name(self));
		vfuncs->finalize(G_OBJECT(self));
	}

	for (guint i = 0; i < FU_PLUGIN_RULE_LAST; i++) {
		if (priv->rules[i] != NULL)
			g_ptr_array_unref(priv->rules[i]);
	}
	if (priv->devices != NULL)
		g_ptr_array_unref(priv->devices);
	if (priv->runtime_versions != NULL)
		g_hash_table_unref(priv->runtime_versions);
	if (priv->compile_versions != NULL)
		g_hash_table_unref(priv->compile_versions);
	if (priv->report_metadata != NULL)
		g_hash_table_unref(priv->report_metadata);
	if (priv->cache != NULL)
		g_hash_table_unref(priv->cache);
	if (priv->device_gtypes != NULL)
		g_array_unref(priv->device_gtypes);
	if (priv->config_monitor != NULL)
		g_object_unref(priv->config_monitor);
	g_free(priv->data);

	G_OBJECT_CLASS(fu_plugin_parent_class)->finalize(object);
}

/**
 * fu_plugin_new_from_gtype:
 * @ctx: (nullable): a #FuContext
 * @gtype: a #GType, possibly even `G_TYPE_PLUGIN`
 *
 * Creates a new #FuPlugin
 *
 * Since: 1.8.6
 **/
FuPlugin *
fu_plugin_new_from_gtype(GType gtype, FuContext *ctx)
{
	FuPlugin *self;

	g_return_val_if_fail(gtype != G_TYPE_INVALID, NULL);
	g_return_val_if_fail(ctx == NULL || FU_IS_CONTEXT(ctx), NULL);

	self = g_object_new(gtype, "context", ctx, NULL);
	if (fu_plugin_get_name(self) == NULL) {
		g_autofree gchar *name = fu_plugin_convert_gtype_to_name(gtype);
		fu_plugin_set_name(self, name);
	}
	return self;
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
fu_plugin_new(FuContext *ctx)
{
	FuPlugin *self = FU_PLUGIN(g_object_new(FU_TYPE_PLUGIN, NULL));
	if (ctx != NULL)
		fu_plugin_set_context(self, ctx);
	return self;
}

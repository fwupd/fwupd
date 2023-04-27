/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuConfig"

#include "config.h"

#include <fcntl.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <unistd.h>

#include "fu-byte-array.h"
#include "fu-bytes.h"
#include "fu-config-private.h"
#include "fu-path.h"
#include "fu-string.h"

enum { SIGNAL_CHANGED, SIGNAL_LOADED, SIGNAL_LAST };

static guint signals[SIGNAL_LAST] = {0};

#define FU_CONFIG_FILE_MODE_SECURE 0640

typedef struct {
	GKeyFile *keyfile;
	GPtrArray *filenames; /* (element-type utf-8) */
	GPtrArray *monitors;  /* (element-type GFileMonitor) */
} FuConfigPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuConfig, fu_config, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_config_get_instance_private(o))

#if !GLIB_CHECK_VERSION(2, 66, 0)

#define G_FILE_SET_CONTENTS_CONSISTENT 0
typedef guint GFileSetContentsFlags;
static gboolean
g_file_set_contents_full(const gchar *filename,
			 const gchar *contents,
			 gssize length,
			 GFileSetContentsFlags flags,
			 int mode,
			 GError **error)
{
	gint fd;
	gssize wrote;

	if (length < 0)
		length = strlen(contents);
	fd = g_open(filename, O_CREAT, mode);
	if (fd <= 0) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "could not open %s file",
			    filename);
		return FALSE;
	}
	wrote = write(fd, contents, length);
	if (wrote != length) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_FAILED,
			    "did not write %s file",
			    filename);
		g_close(fd, NULL);
		return FALSE;
	}
	return g_close(fd, error);
}
#endif

static void
fu_config_emit_changed(FuConfig *self)
{
	g_debug("::configuration changed");
	g_signal_emit(self, signals[SIGNAL_CHANGED], 0);
}

static void
fu_config_emit_loaded(FuConfig *self)
{
	g_debug("::configuration loaded");
	g_signal_emit(self, signals[SIGNAL_LOADED], 0);
}

static gboolean
fu_config_reload(FuConfig *self, GError **error)
{
	FuConfigPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GByteArray) buf = g_byte_array_new();
	g_autoptr(GPtrArray) legacy_cfg_files = g_ptr_array_new_with_free_func(g_free);
	const gchar *fn_merge[] = {"daemon.conf",
				   "msr.conf",
				   "redfish.conf",
				   "thunderbolt.conf",
				   "uefi_capsule.conf",
				   NULL};

	/* ensure the config files are set to the correct permissions */
	for (guint i = 0; i < priv->filenames->len; i++) {
		const gchar *fn = g_ptr_array_index(priv->filenames, i);
		if (g_file_test(fn, G_FILE_TEST_EXISTS)) {
			GStatBuf st = {0x0};
			gint rc = g_stat(fn, &st);
			if (rc != 0) {
				g_set_error(error,
					    G_IO_ERROR,
					    G_IO_ERROR_FAILED,
					    "failed to get permission of %s",
					    fn);
				return FALSE;
			}
			st.st_mode &= 0777;
			if (st.st_mode != FU_CONFIG_FILE_MODE_SECURE) {
				g_info("mode was 0%o, and needs to be 0%o",
				       st.st_mode,
				       (guint)FU_CONFIG_FILE_MODE_SECURE);
				rc = g_chmod(fn, FU_CONFIG_FILE_MODE_SECURE);
				if (rc != 0) {
					g_set_error(error,
						    G_IO_ERROR,
						    G_IO_ERROR_FAILED,
						    "failed to change permission of %s",
						    fn);
					return FALSE;
				}
			}
		}
	}

	/* we have to load each file into a buffer as g_key_file_load_from_file() clears the
	 * GKeyFile state before loading each file, and we want to allow the mutable version to be
	 * incomplete and just *override* a specific option */
	for (guint i = 0; i < priv->filenames->len; i++) {
		const gchar *fn = g_ptr_array_index(priv->filenames, i);
		g_autofree gchar *dirname = g_path_get_dirname(fn);
		g_debug("trying to load config values from %s", fn);
		if (g_file_test(fn, G_FILE_TEST_EXISTS)) {
			g_autoptr(GBytes) blob = fu_bytes_get_contents(fn, error);
			if (blob == NULL)
				return FALSE;
			fu_byte_array_append_bytes(buf, blob);
		}

		/* are any of the legacy files found in this location? */
		for (guint j = 0; fn_merge[j] != NULL; j++) {
			g_autofree gchar *fncompat = g_build_filename(dirname, fn_merge[j], NULL);
			if (g_file_test(fncompat, G_FILE_TEST_EXISTS)) {
				g_autoptr(GBytes) blob = fu_bytes_get_contents(fncompat, error);
				if (blob == NULL)
					return FALSE;
				fu_byte_array_append_bytes(buf, blob);
				g_ptr_array_add(legacy_cfg_files, g_steal_pointer(&fncompat));
			}
		}
	}

	/* load if either file found */
	if (buf->len > 0) {
		if (!g_key_file_load_from_data(priv->keyfile,
					       (const gchar *)buf->data,
					       buf->len,
					       G_KEY_FILE_NONE,
					       error))
			return FALSE;
	}

	/* migration needed */
	if (legacy_cfg_files->len > 0) {
		const gchar *fn_default = g_ptr_array_index(priv->filenames, 0);
		g_autofree gchar *data = NULL;

		/* do not write empty keys migrated from daemon.conf */
		struct {
			const gchar *group;
			const gchar *key;
			const gchar *value;
		} key_values[] = {{"fwupd", "ApprovedFirmware", ""},
				  {"fwupd", "ArchiveSizeMax", "0"},
				  {"fwupd", "BlockedFirmware", ""},
				  {"fwupd", "DisabledDevices", ""},
				  {"fwupd", "EnumerateAllDevices", "false"},
				  {"fwupd", "EspLocation", ""},
				  {"fwupd", "HostBkc", ""},
				  {"fwupd", "IdleTimeout", "7200"},
				  {"fwupd", "IgnorePower", ""},
				  {"fwupd", "ShowDevicePrivate", "true"},
				  {"fwupd", "TrustedUids", ""},
				  {"fwupd", "UpdateMotd", "true"},
				  {"fwupd", "UriSchemes", ""},
				  {"fwupd", "VerboseDomains", ""},
				  {"redfish", "IpmiDisableCreateUser", "False"},
				  {"redfish", "ManagerResetTimeout", "1800"},
				  {NULL, NULL, NULL}};
		for (guint i = 0; key_values[i].group != NULL; i++) {
			g_autofree gchar *value = g_key_file_get_value(priv->keyfile,
								       key_values[i].group,
								       key_values[i].key,
								       NULL);
			if (g_strcmp0(value, key_values[i].value) == 0) {
				g_debug("not migrating default value of [%s] %s=%s",
					key_values[i].group,
					key_values[i].key,
					key_values[i].value);
				g_key_file_remove_key(priv->keyfile,
						      key_values[i].group,
						      key_values[i].key,
						      NULL);
			}
		}

		/* make sure we can save the new file first */
		data = g_key_file_to_data(priv->keyfile, NULL, error);
		if (data == NULL)
			return FALSE;
		if (!g_file_set_contents_full(
			fn_default,
			data,
			-1,
			G_FILE_SET_CONTENTS_CONSISTENT,
			FU_CONFIG_FILE_MODE_SECURE, /* only readable by root */
			error))
			return FALSE;

		/* give the legacy files a .old extension */
		for (guint i = 0; i < legacy_cfg_files->len; i++) {
			const gchar *fn_old = g_ptr_array_index(legacy_cfg_files, i);
			g_autofree gchar *fn_new = g_strdup_printf("%s.old", fn_old);
			g_info("renaming legacy config file %s to %s", fn_old, fn_new);
			if (g_rename(fn_old, fn_new) != 0) {
				g_set_error(error,
					    G_IO_ERROR,
					    G_IO_ERROR_FAILED,
					    "failed to change rename %s to %s",
					    fn_old,
					    fn_new);
				return FALSE;
			}
		}
	}

	/* success */
	return TRUE;
}

static void
fu_config_monitor_changed_cb(GFileMonitor *monitor,
			     GFile *file,
			     GFile *other_file,
			     GFileMonitorEvent event_type,
			     gpointer user_data)
{
	FuConfig *self = FU_CONFIG(user_data);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *fn = g_file_get_path(file);
	g_info("%s changed, reloading all configs", fn);
	if (!fu_config_reload(self, &error))
		g_warning("failed to rescan daemon config: %s", error->message);
	fu_config_emit_changed(self);
}

/**
 * fu_config_set_value:
 * @self: a #FuConfig
 * @section: a settings section
 * @key: a settings key
 * @value: (nullable): a settings value
 * @error: (nullable): optional return location for an error
 *
 * Sets a plugin config value, saving the new data back to the default config file.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.1
 **/
gboolean
fu_config_set_value(FuConfig *self,
		    const gchar *section,
		    const gchar *key,
		    const gchar *value,
		    GError **error)
{
	FuConfigPrivate *priv = GET_PRIVATE(self);
	const gchar *fn;
	g_autofree gchar *data = NULL;

	g_return_val_if_fail(FU_IS_CONFIG(self), FALSE);
	g_return_val_if_fail(section != NULL, FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* sanity check */
	if (priv->filenames->len == 0) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED, "no config to load");
		return FALSE;
	}

	/* only write the file in /etc */
	fn = g_ptr_array_index(priv->filenames, 0);
	g_key_file_set_string(priv->keyfile, section, key, value);
	data = g_key_file_to_data(priv->keyfile, NULL, error);
	if (data == NULL)
		return FALSE;
	if (!g_file_set_contents_full(fn,
				      data,
				      -1,
				      G_FILE_SET_CONTENTS_CONSISTENT,
				      FU_CONFIG_FILE_MODE_SECURE, /* only readable by root */
				      error))
		return FALSE;
	return fu_config_reload(self, error);
}

/**
 * fu_config_get_value:
 * @self: a #FuConfig
 * @section: a settings section
 * @key: a settings key
 * @value_default: (nullable): a settings value default
 *
 * Return the value of a key, falling back to the default value if missing.
 *
 * NOTE: this function will return an empty string for `key=`.
 *
 * Returns: (transfer full): key value
 *
 * Since: 1.9.1
 **/
gchar *
fu_config_get_value(FuConfig *self,
		    const gchar *section,
		    const gchar *key,
		    const gchar *value_default)
{
	FuConfigPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *value = NULL;

	g_return_val_if_fail(FU_IS_CONFIG(self), NULL);
	g_return_val_if_fail(section != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);

	value = g_key_file_get_string(priv->keyfile, section, key, NULL);
	if (value == NULL)
		return g_strdup(value_default);
	return g_steal_pointer(&value);
}

/**
 * fu_config_get_value_strv:
 * @self: a #FuConfig
 * @section: a settings section
 * @key: a settings key
 * @value_default: (nullable): a settings value default
 *
 * Return the value of a key, falling back to the default value if missing.
 *
 * NOTE: this function will return an empty string for `key=`.
 *
 * Returns: (transfer full) (nullable): key value
 *
 * Since: 1.9.1
 **/
gchar **
fu_config_get_value_strv(FuConfig *self,
			 const gchar *section,
			 const gchar *key,
			 const gchar *value_default)
{
	g_autofree gchar *value = NULL;
	g_return_val_if_fail(FU_IS_CONFIG(self), NULL);
	g_return_val_if_fail(section != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);
	value = fu_config_get_value(self, section, key, value_default);
	if (value == NULL)
		return NULL;
	return g_strsplit(value, ";", -1);
}

/**
 * fu_config_get_value_bool:
 * @self: a #FuConfig
 * @section: a settings section
 * @key: a settings key
 * @value_default: a settings value default
 *
 * Return the value of a key, falling back to the default value if missing or empty.
 *
 * Returns: boolean
 *
 * Since: 1.9.1
 **/
gboolean
fu_config_get_value_bool(FuConfig *self,
			 const gchar *section,
			 const gchar *key,
			 gboolean value_default)
{
	g_autofree gchar *tmp = fu_config_get_value(self, section, key, NULL);
	if (tmp == NULL || tmp[0] == '\0')
		return value_default;
	return g_ascii_strcasecmp(tmp, "true") == 0;
}

/**
 * fu_config_get_value_u64:
 * @self: a #FuConfig
 * @section: a settings section
 * @key: a settings key
 * @value_default: a settings value default
 *
 * Return the value of a key, falling back to the default value if missing or empty.
 *
 * Returns: uint64
 *
 * Since: 1.9.1
 **/
guint64
fu_config_get_value_u64(FuConfig *self,
			const gchar *section,
			const gchar *key,
			guint64 value_default)
{
	guint64 value = 0;
	g_autofree gchar *tmp = fu_config_get_value(self, section, key, NULL);
	g_autoptr(GError) error_local = NULL;

	if (tmp == NULL || tmp[0] == '\0')
		return value_default;
	if (!fu_strtoull(tmp, &value, 0, G_MAXUINT64, &error_local)) {
		g_warning("failed to parse [%s] %s = %s as integer", section, key, tmp);
		return value_default;
	}
	return value;
}

/**
 * fu_config_load:
 * @self: a #FuConfig
 * @error: (nullable): optional return location for an error
 *
 * Loads the configuration files from all possible locations.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.9.1
 **/
gboolean
fu_config_load(FuConfig *self, GError **error)
{
	FuConfigPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *configdir_mut = fu_path_from_kind(FU_PATH_KIND_LOCALCONFDIR_PKG);
	g_autofree gchar *configdir = fu_path_from_kind(FU_PATH_KIND_SYSCONFDIR_PKG);

	g_return_val_if_fail(FU_IS_CONFIG(self), FALSE);
	g_return_val_if_fail(priv->filenames->len == 0, FALSE);

	/* load the main daemon config file */
	g_ptr_array_add(priv->filenames, g_build_filename(configdir, "fwupd.conf", NULL));
	g_ptr_array_add(priv->filenames, g_build_filename(configdir_mut, "fwupd.conf", NULL));
	if (!fu_config_reload(self, error))
		return FALSE;

	/* set up a notify watches */
	for (guint i = 0; i < priv->filenames->len; i++) {
		const gchar *fn = g_ptr_array_index(priv->filenames, i);
		g_autoptr(GFile) file = g_file_new_for_path(fn);
		g_autoptr(GFileMonitor) monitor = NULL;

		monitor = g_file_monitor(file, G_FILE_MONITOR_NONE, NULL, error);
		if (monitor == NULL)
			return FALSE;
		g_signal_connect(G_FILE_MONITOR(monitor),
				 "changed",
				 G_CALLBACK(fu_config_monitor_changed_cb),
				 self);
		g_ptr_array_add(priv->monitors, g_steal_pointer(&monitor));
	}

	/* success */
	fu_config_emit_loaded(self);
	return TRUE;
}

static void
fu_config_init(FuConfig *self)
{
	FuConfigPrivate *priv = GET_PRIVATE(self);
	priv->keyfile = g_key_file_new();
	priv->filenames = g_ptr_array_new_with_free_func(g_free);
	priv->monitors = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
}

static void
fu_config_finalize(GObject *obj)
{
	FuConfig *self = FU_CONFIG(obj);
	FuConfigPrivate *priv = GET_PRIVATE(self);
	for (guint i = 0; i < priv->monitors->len; i++) {
		GFileMonitor *monitor = g_ptr_array_index(priv->monitors, i);
		g_file_monitor_cancel(monitor);
	}
	g_key_file_unref(priv->keyfile);
	g_ptr_array_unref(priv->filenames);
	g_ptr_array_unref(priv->monitors);
	G_OBJECT_CLASS(fu_config_parent_class)->finalize(obj);
}

static void
fu_config_class_init(FuConfigClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_config_finalize;

	/**
	 * FuConfig::changed:
	 * @self: the #FuConfig instance that emitted the signal
	 *
	 * The ::changed signal is emitted when the config file has
	 * changed, for instance when a parameter has been modified.
	 **/
	signals[SIGNAL_CHANGED] = g_signal_new("changed",
					       G_TYPE_FROM_CLASS(object_class),
					       G_SIGNAL_RUN_LAST,
					       0,
					       NULL,
					       NULL,
					       g_cclosure_marshal_VOID__VOID,
					       G_TYPE_NONE,
					       0);

	/**
	 * FuConfig::loaded:
	 * @self: the #FuConfig instance that emitted the signal
	 *
	 * The ::loaded signal is emitted when the config file has
	 * loaded, typically at startup.
	 **/
	signals[SIGNAL_LOADED] = g_signal_new("loaded",
					      G_TYPE_FROM_CLASS(object_class),
					      G_SIGNAL_RUN_LAST,
					      0,
					      NULL,
					      NULL,
					      g_cclosure_marshal_VOID__VOID,
					      G_TYPE_NONE,
					      0);
}

/**
 * fu_config_new:
 *
 * Creates a new #FuConfig.
 *
 * Returns: (transfer full): a new #FuConfig
 *
 * Since: 1.9.1
 **/
FuConfig *
fu_config_new(void)
{
	return FU_CONFIG(g_object_new(FU_TYPE_CONFIG, NULL));
}

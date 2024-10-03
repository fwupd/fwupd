/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
	gchar *filename;
	GFile *file;
	GFileMonitor *monitor; /* nullable */
	gboolean is_writable;
	gboolean is_mutable;
} FuConfigItem;

typedef struct {
	GKeyFile *keyfile;
	GHashTable *default_values;
	GPtrArray *items; /* (element-type FuConfigItem) */
} FuConfigPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FuConfig, fu_config, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (fu_config_get_instance_private(o))

static void
fu_config_item_free(FuConfigItem *item)
{
	if (item->monitor != NULL) {
		g_file_monitor_cancel(item->monitor);
		g_object_unref(item->monitor);
	}
	g_object_unref(item->file);
	g_free(item->filename);
	g_free(item);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuConfigItem, fu_config_item_free)

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

static gchar *
fu_config_build_section_key(const gchar *section, const gchar *key)
{
	return g_strdup_printf("%s::%s", section, key);
}

static gboolean
fu_config_load_bytes_replace(FuConfig *self, GBytes *blob, GError **error)
{
	FuConfigPrivate *priv = GET_PRIVATE(self);
	g_auto(GStrv) groups = NULL;
	g_autoptr(GKeyFile) kf = g_key_file_new();

	if (!g_key_file_load_from_data(kf,
				       (const gchar *)g_bytes_get_data(blob, NULL),
				       g_bytes_get_size(blob),
				       G_KEY_FILE_KEEP_COMMENTS,
				       error))
		return FALSE;
	groups = g_key_file_get_groups(kf, NULL);
	for (guint i = 0; groups[i] != NULL; i++) {
		g_auto(GStrv) keys = NULL;
		g_autofree gchar *comment_group = NULL;
		keys = g_key_file_get_keys(kf, groups[i], NULL, error);
		if (keys == NULL) {
			g_prefix_error(error, "failed to get keys for [%s]: ", groups[i]);
			return FALSE;
		}
		for (guint j = 0; keys[j] != NULL; j++) {
			const gchar *default_value;
			g_autofree gchar *comment_key = NULL;
			g_autofree gchar *section_key = NULL;
			g_autofree gchar *value = NULL;

			value = g_key_file_get_string(kf, groups[i], keys[j], error);
			if (value == NULL) {
				g_prefix_error(error,
					       "failed to get string for %s=%s: ",
					       groups[i],
					       keys[j]);
				return FALSE;
			}

			/* is the same as the default */
			section_key = fu_config_build_section_key(groups[i], keys[j]);
			default_value = g_hash_table_lookup(priv->default_values, section_key);
			if (g_strcmp0(value, default_value) == 0) {
				g_debug("default config, ignoring [%s] %s=%s",
					groups[i],
					keys[j],
					value);
				continue;
			}

			g_debug("setting config [%s] %s=%s", groups[i], keys[j], value);
			g_key_file_set_string(priv->keyfile, groups[i], keys[j], value);
			comment_key = g_key_file_get_comment(kf, groups[i], keys[j], NULL);
			if (comment_key != NULL && comment_key[0] != '\0') {
				if (!g_key_file_set_comment(priv->keyfile,
							    groups[i],
							    keys[j],
							    comment_key,
							    error)) {
					g_prefix_error(error,
						       "failed to set comment '%s' for %s=%s: ",
						       comment_key,
						       groups[i],
						       keys[j]);
					return FALSE;
				}
			}
		}
		comment_group = g_key_file_get_comment(kf, groups[i], NULL, NULL);
		if (comment_group != NULL && comment_group[0] != '\0') {
			if (!g_key_file_set_comment(priv->keyfile,
						    groups[i],
						    NULL,
						    comment_group,
						    error)) {
				g_prefix_error(error,
					       "failed to set comment '%s' for [%s]: ",
					       comment_group,
					       groups[i]);
				return FALSE;
			}
		}
	}

	/* success */
	return TRUE;
}

static void
fu_config_migrate_keyfile(FuConfig *self)
{
	FuConfigPrivate *priv = GET_PRIVATE(self);
	struct {
		const gchar *group;
		const gchar *key;
		const gchar *value;
	} key_values[] = {{"fwupd", "ApprovedFirmware", NULL},
			  {"fwupd", "ArchiveSizeMax", "0"},
			  {"fwupd", "BlockedFirmware", NULL},
			  {"fwupd", "DisabledDevices", NULL},
			  {"fwupd", "EmulatedDevices", NULL},
			  {"fwupd", "EnumerateAllDevices", NULL},
			  {"fwupd", "EspLocation", NULL},
			  {"fwupd", "HostBkc", NULL},
			  {"fwupd", "IdleTimeout", "7200"},
			  {"fwupd", "IdleTimeout", NULL},
			  {"fwupd", "IgnorePower", NULL},
			  {"fwupd", "ShowDevicePrivate", NULL},
			  {"fwupd", "TrustedUids", NULL},
			  {"fwupd", "UpdateMotd", NULL},
			  {"fwupd", "UriSchemes", NULL},
			  {"fwupd", "VerboseDomains", NULL},
			  {"fwupd", "OnlyTrusted", NULL},
			  {"fwupd", "IgnorePower", NULL},
			  {"fwupd", "DisabledPlugins", "test;test_ble;invalid"},
			  {"fwupd", "DisabledPlugins", "test;test_ble"},
			  {"fwupd", "AllowEmulation", NULL},
			  {"redfish", "IpmiDisableCreateUser", NULL},
			  {"redfish", "ManagerResetTimeout", NULL},
			  {"msr", "MinimumSmeKernelVersion", NULL},
			  {"thunderbolt", "MinimumKernelVersion", NULL},
			  {"thunderbolt", "DelayedActivation", NULL},
			  {NULL, NULL, NULL}};
	for (guint i = 0; key_values[i].group != NULL; i++) {
		const gchar *default_value;
		g_autofree gchar *value = NULL;
		g_auto(GStrv) keys = NULL;

		value = g_key_file_get_value(priv->keyfile,
					     key_values[i].group,
					     key_values[i].key,
					     NULL);
		if (value == NULL)
			continue;
		if (key_values[i].value == NULL) {
			g_autofree gchar *section_key =
			    fu_config_build_section_key(key_values[i].group, key_values[i].key);
			default_value = g_hash_table_lookup(priv->default_values, section_key);
		} else {
			default_value = key_values[i].value;
		}
		if ((default_value != NULL && g_ascii_strcasecmp(value, default_value) == 0) ||
		    (key_values[i].value == NULL && g_strcmp0(value, "") == 0)) {
			g_debug("not migrating default value of [%s] %s=%s",
				key_values[i].group,
				key_values[i].key,
				default_value);
			g_key_file_remove_comment(priv->keyfile,
						  key_values[i].group,
						  key_values[i].key,
						  NULL);
			g_key_file_remove_key(priv->keyfile,
					      key_values[i].group,
					      key_values[i].key,
					      NULL);
		}

		/* remove the group if there are no keys left */
		keys = g_key_file_get_keys(priv->keyfile, key_values[i].group, NULL, NULL);
		if (g_strv_length(keys) == 0) {
			g_key_file_remove_comment(priv->keyfile, key_values[i].group, NULL, NULL);
			g_key_file_remove_group(priv->keyfile, key_values[i].group, NULL);
		}
	}
}

static gboolean
fu_config_reload(FuConfig *self, GError **error)
{
	FuConfigPrivate *priv = GET_PRIVATE(self);
	g_autoptr(GPtrArray) legacy_cfg_files = g_ptr_array_new_with_free_func(g_free);
	const gchar *fn_merge[] = {"daemon.conf",
				   "msr.conf",
				   "redfish.conf",
				   "thunderbolt.conf",
				   "uefi_capsule.conf",
				   NULL};

#ifndef _WIN32
	/* ensure mutable config files are set to the correct permissions */
	for (guint i = 0; i < priv->items->len; i++) {
		FuConfigItem *item = g_ptr_array_index(priv->items, i);
		guint32 st_mode;
		g_autoptr(GFileInfo) info = NULL;

		/* check permissions */
		if (!item->is_writable) {
			g_debug("skipping mode check for %s as not writable", item->filename);
			continue;
		}
		info = g_file_query_info(item->file,
					 G_FILE_ATTRIBUTE_UNIX_MODE,
					 G_FILE_QUERY_INFO_NONE,
					 NULL,
					 error);
		if (info == NULL) {
			g_prefix_error(error, "failed to query info about %s", item->filename);
			return FALSE;
		}
		st_mode = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_MODE) & 0777;
		if (st_mode != FU_CONFIG_FILE_MODE_SECURE) {
			g_info("fixing %s from mode 0%o to 0%o",
			       item->filename,
			       st_mode,
			       (guint)FU_CONFIG_FILE_MODE_SECURE);
			g_file_info_set_attribute_uint32(info,
							 G_FILE_ATTRIBUTE_UNIX_MODE,
							 FU_CONFIG_FILE_MODE_SECURE);
			if (!g_file_set_attributes_from_info(item->file,
							     info,
							     G_FILE_QUERY_INFO_NONE,
							     NULL,
							     error)) {
				g_prefix_error(error,
					       "failed to set mode attribute of %s: ",
					       item->filename);
				return FALSE;
			}
		}
	}
#endif

	/* we have to copy each group/key from a temporary GKeyFile as g_key_file_load_from_file()
	 * clears all keys before loading each file, and we want to allow the mutable version to be
	 * incomplete and just *override* a specific option */
	if (!g_key_file_load_from_data(priv->keyfile, "", -1, G_KEY_FILE_NONE, error))
		return FALSE;
	for (guint i = 0; i < priv->items->len; i++) {
		FuConfigItem *item = g_ptr_array_index(priv->items, i);
		g_autoptr(GError) error_load = NULL;
		g_autoptr(GBytes) blob_item = NULL;

		g_debug("trying to load config values from %s", item->filename);
		blob_item = fu_bytes_get_contents(item->filename, &error_load);
		if (blob_item == NULL) {
			if (g_error_matches(error_load,
					    FWUPD_ERROR,
					    FWUPD_ERROR_PERMISSION_DENIED)) {
				g_debug("ignoring config file %s: ", error_load->message);
				continue;
			} else if (g_error_matches(error_load,
						   FWUPD_ERROR,
						   FWUPD_ERROR_INVALID_FILE)) {
				g_debug("%s", error_load->message);
				continue;
			}
			g_propagate_error(error, g_steal_pointer(&error_load));
			g_prefix_error(error, "failed to read %s: ", item->filename);
			return FALSE;
		}
		if (!fu_config_load_bytes_replace(self, blob_item, error)) {
			g_prefix_error(error, "failed to load %s: ", item->filename);
			return FALSE;
		}
	}

	/* are any of the legacy files found in this location? */
	for (guint i = 0; i < priv->items->len; i++) {
		FuConfigItem *item = g_ptr_array_index(priv->items, i);
		g_autofree gchar *dirname = g_path_get_dirname(item->filename);

		for (guint j = 0; fn_merge[j] != NULL; j++) {
			g_autofree gchar *fncompat = g_build_filename(dirname, fn_merge[j], NULL);
			if (g_file_test(fncompat, G_FILE_TEST_EXISTS)) {
				g_autoptr(GBytes) blob_compat =
				    fu_bytes_get_contents(fncompat, error);
				if (blob_compat == NULL) {
					g_prefix_error(error, "failed to read %s: ", fncompat);
					return FALSE;
				}
				if (!fu_config_load_bytes_replace(self, blob_compat, error)) {
					g_prefix_error(error, "failed to load %s: ", fncompat);
					return FALSE;
				}
				g_ptr_array_add(legacy_cfg_files, g_steal_pointer(&fncompat));
			}
		}
	}

	/* migration needed */
	if (legacy_cfg_files->len > 0) {
		FuConfigItem *item = g_ptr_array_index(priv->items, 0);
		const gchar *fn_default = item->filename;
		g_autofree gchar *data = NULL;

		/* do not write empty keys migrated from daemon.conf */
		fu_config_migrate_keyfile(self);

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
			error)) {
			g_prefix_error(error, "failed to save %s: ", fn_default);
			return FALSE;
		}

		/* give the legacy files a .old extension */
		for (guint i = 0; i < legacy_cfg_files->len; i++) {
			const gchar *fn_old = g_ptr_array_index(legacy_cfg_files, i);
			g_autofree gchar *fn_new = g_strdup_printf("%s.old", fn_old);
			g_info("renaming legacy config file %s to %s", fn_old, fn_new);
			if (g_rename(fn_old, fn_new) != 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INVALID_FILE,
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

	/* nothing we need to care about */
	if (event_type == G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED) {
		g_debug("%s attributes changed, ignoring", fn);
		return;
	}

	/* reload everything */
	g_info("%s changed, reloading all configs", fn);
	if (!fu_config_reload(self, &error))
		g_warning("failed to rescan daemon config: %s", error->message);
	fu_config_emit_changed(self);
}

/**
 * fu_config_set_default:
 * @self: a #FuConfig
 * @section: a settings section
 * @key: a settings key
 * @value: (nullable): a settings value
 *
 * Sets a default config value.
 *
 * Since: 2.0.0
 **/
void
fu_config_set_default(FuConfig *self, const gchar *section, const gchar *key, const gchar *value)
{
	FuConfigPrivate *priv = GET_PRIVATE(self);
	g_return_if_fail(FU_IS_CONFIG(self));
	g_return_if_fail(section != NULL);
	g_return_if_fail(key != NULL);
	g_hash_table_insert(priv->default_values,
			    fu_config_build_section_key(section, key),
			    g_strdup(value));
}

static gboolean
fu_config_save(FuConfig *self, GError **error)
{
	FuConfigPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *data = NULL;

	data = g_key_file_to_data(priv->keyfile, NULL, error);
	if (data == NULL)
		return FALSE;
	for (guint i = 0; i < priv->items->len; i++) {
		FuConfigItem *item = g_ptr_array_index(priv->items, i);
		if (!item->is_mutable)
			continue;
		if (!fu_path_mkdir_parent(item->filename, error))
			return FALSE;
		if (!g_file_set_contents_full(item->filename,
					      data,
					      -1,
					      G_FILE_SET_CONTENTS_CONSISTENT,
					      FU_CONFIG_FILE_MODE_SECURE, /* only for root */
					      error))
			return FALSE;
		return fu_config_reload(self, error);
	}

	/* failed */
	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no writable config");
	return FALSE;
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

	g_return_val_if_fail(FU_IS_CONFIG(self), FALSE);
	g_return_val_if_fail(section != NULL, FALSE);
	g_return_val_if_fail(key != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* sanity check */
	if (priv->items->len == 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "no config to load");
		return FALSE;
	}

	/* do not write default keys */
	fu_config_migrate_keyfile(self);

	/* only write the file to a mutable location */
	g_key_file_set_string(priv->keyfile, section, key, value);
	return fu_config_save(self, error);
}

/**
 * fu_config_reset_defaults:
 * @self: a #FuConfig
 * @section: a settings section
 *
 * Reset all the keys back to the default values.
 *
 * Since: 1.9.15
 **/
gboolean
fu_config_reset_defaults(FuConfig *self, const gchar *section, GError **error)
{
	FuConfigPrivate *priv = GET_PRIVATE(self);

	g_return_val_if_fail(FU_IS_CONFIG(self), FALSE);
	g_return_val_if_fail(section != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* remove all keys, and save */
	g_key_file_remove_group(priv->keyfile, section, NULL);
	return fu_config_save(self, error);
}

/**
 * fu_config_get_value:
 * @self: a #FuConfig
 * @section: a settings section
 * @key: a settings key
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
fu_config_get_value(FuConfig *self, const gchar *section, const gchar *key)
{
	FuConfigPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *value = NULL;

	g_return_val_if_fail(FU_IS_CONFIG(self), NULL);
	g_return_val_if_fail(section != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);

	value = g_key_file_get_string(priv->keyfile, section, key, NULL);
	if (value == NULL) {
		g_autofree gchar *section_key = fu_config_build_section_key(section, key);
		const gchar *value_tmp = g_hash_table_lookup(priv->default_values, section_key);
		return g_strdup(value_tmp);
	}
	return g_steal_pointer(&value);
}

/**
 * fu_config_get_value_strv:
 * @self: a #FuConfig
 * @section: a settings section
 * @key: a settings key
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
fu_config_get_value_strv(FuConfig *self, const gchar *section, const gchar *key)
{
	g_autofree gchar *value = NULL;
	g_return_val_if_fail(FU_IS_CONFIG(self), NULL);
	g_return_val_if_fail(section != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);
	value = fu_config_get_value(self, section, key);
	if (value == NULL)
		return NULL;
	return g_strsplit(value, ";", -1);
}

/**
 * fu_config_get_value_bool:
 * @self: a #FuConfig
 * @section: a settings section
 * @key: a settings key
 *
 * Return the value of a key, falling back to the default value if missing or empty.
 *
 * Returns: boolean
 *
 * Since: 1.9.1
 **/
gboolean
fu_config_get_value_bool(FuConfig *self, const gchar *section, const gchar *key)
{
	FuConfigPrivate *priv = GET_PRIVATE(self);
	g_autofree gchar *value = fu_config_get_value(self, section, key);
	if (value == NULL || value[0] == '\0') {
		g_autofree gchar *section_key = fu_config_build_section_key(section, key);
		const gchar *value_tmp = g_hash_table_lookup(priv->default_values, section_key);
		if (value_tmp == NULL) {
			g_critical("no default for [%s] %s", section, key);
			return FALSE;
		}
		return g_ascii_strcasecmp(value_tmp, "true") == 0;
	}
	return g_ascii_strcasecmp(value, "true") == 0;
}

/**
 * fu_config_get_value_u64:
 * @self: a #FuConfig
 * @section: a settings section
 * @key: a settings key
 *
 * Return the value of a key, falling back to the default value if missing or empty.
 *
 * Returns: uint64
 *
 * Since: 1.9.1
 **/
guint64
fu_config_get_value_u64(FuConfig *self, const gchar *section, const gchar *key)
{
	FuConfigPrivate *priv = GET_PRIVATE(self);
	guint64 value = 0;
	const gchar *value_tmp;
	g_autofree gchar *tmp = fu_config_get_value(self, section, key);
	g_autoptr(GError) error_local = NULL;

	if (tmp == NULL || tmp[0] == '\0') {
		g_autofree gchar *section_key = fu_config_build_section_key(section, key);
		value_tmp = g_hash_table_lookup(priv->default_values, section_key);
		if (value_tmp == NULL) {
			g_critical("no default for [%s] %s", section, key);
			return G_MAXUINT64;
		}
	} else {
		value_tmp = tmp;
	}
	if (!fu_strtoull(value_tmp, &value, 0, G_MAXUINT64, FU_INTEGER_BASE_AUTO, &error_local)) {
		g_warning("failed to parse [%s] %s = %s as integer", section, key, value_tmp);
		return G_MAXUINT64;
	}
	return value;
}

static gboolean
fu_config_add_location(FuConfig *self, const gchar *dirname, gboolean is_mutable, GError **error)
{
	FuConfigPrivate *priv = GET_PRIVATE(self);
	g_autoptr(FuConfigItem) item = g_new0(FuConfigItem, 1);

	item->is_mutable = is_mutable;
	item->filename = g_build_filename(dirname, "fwupd.conf", NULL);
	item->file = g_file_new_for_path(item->filename);

	/* is writable */
	if (g_file_query_exists(item->file, NULL)) {
		g_autoptr(GFileInfo) info = NULL;

		g_debug("loading config %s", item->filename);
		info = g_file_query_info(item->file,
					 G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE,
					 G_FILE_QUERY_INFO_NONE,
					 NULL,
					 error);
		if (info == NULL)
			return FALSE;
		item->is_writable =
		    g_file_info_get_attribute_boolean(info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE);
		if (!item->is_writable)
			g_debug("config %s is immutable", item->filename);
	} else {
		g_debug("not loading config %s", item->filename);
	}

	/* success */
	g_ptr_array_add(priv->items, g_steal_pointer(&item));
	return TRUE;
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
	g_return_val_if_fail(priv->items->len == 0, FALSE);

	/* load the main daemon config file */
	if (!fu_config_add_location(self, configdir, FALSE, error))
		return FALSE;
	if (!fu_config_add_location(self, configdir_mut, TRUE, error))
		return FALSE;
	if (!fu_config_reload(self, error))
		return FALSE;

	/* set up a notify watches */
	for (guint i = 0; i < priv->items->len; i++) {
		FuConfigItem *item = g_ptr_array_index(priv->items, i);
		g_autoptr(GFile) file = g_file_new_for_path(item->filename);
		item->monitor = g_file_monitor(file, G_FILE_MONITOR_NONE, NULL, error);
		if (item->monitor == NULL)
			return FALSE;
		g_signal_connect(G_FILE_MONITOR(item->monitor),
				 "changed",
				 G_CALLBACK(fu_config_monitor_changed_cb),
				 self);
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
	priv->items = g_ptr_array_new_with_free_func((GDestroyNotify)fu_config_item_free);
	priv->default_values = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
}

static void
fu_config_finalize(GObject *obj)
{
	FuConfig *self = FU_CONFIG(obj);
	FuConfigPrivate *priv = GET_PRIVATE(self);
	g_key_file_unref(priv->keyfile);
	g_ptr_array_unref(priv->items);
	g_hash_table_unref(priv->default_values);
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

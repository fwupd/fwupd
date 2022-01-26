/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuConfig"

#include "config.h"

#include <gio/gio.h>
#include <glib-object.h>

#include "fu-common.h"
#include "fu-config.h"

enum { SIGNAL_CHANGED, SIGNAL_LAST };

static guint signals[SIGNAL_LAST] = {0};

static void
fu_config_finalize(GObject *obj);

struct _FuConfig {
	GObject parent_instance;
	GPtrArray *monitors;	      /* (element-type GFileMonitor) */
	GPtrArray *disabled_devices;  /* (element-type utf-8) */
	GPtrArray *disabled_plugins;  /* (element-type utf-8) */
	GPtrArray *approved_firmware; /* (element-type utf-8) */
	GPtrArray *blocked_firmware;  /* (element-type utf-8) */
	GPtrArray *uri_schemes;	      /* (element-type utf-8) */
	GPtrArray *filenames;	      /* (element-type utf-8) */
	guint64 archive_size_max;
	guint idle_timeout;
	gchar *host_bkc;
	gboolean update_motd;
	gboolean enumerate_all_devices;
	gboolean ignore_power;
	gboolean only_trusted;
};

G_DEFINE_TYPE(FuConfig, fu_config, G_TYPE_OBJECT)

static void
fu_config_emit_changed(FuConfig *self)
{
	g_debug("::configuration changed");
	g_signal_emit(self, signals[SIGNAL_CHANGED], 0);
}

static gboolean
fu_config_reload(FuConfig *self, GError **error)
{
	guint64 archive_size_max;
	guint idle_timeout;
	g_auto(GStrv) approved_firmware = NULL;
	g_auto(GStrv) blocked_firmware = NULL;
	g_auto(GStrv) uri_schemes = NULL;
	g_auto(GStrv) devices = NULL;
	g_auto(GStrv) plugins = NULL;
	g_autofree gchar *domains = NULL;
	g_autofree gchar *host_bkc = NULL;
	g_autoptr(GKeyFile) keyfile = g_key_file_new();
	g_autoptr(GError) error_update_motd = NULL;
	g_autoptr(GError) error_ignore_power = NULL;
	g_autoptr(GError) error_only_trusted = NULL;
	g_autoptr(GError) error_enumerate_all = NULL;
	g_autoptr(GByteArray) buf = g_byte_array_new();

	/* we have to load each file into a buffer as g_key_file_load_from_file() clears the
	 * GKeyFile state before loading each file, and we want to allow the mutable version to be
	 * incomplete and just *override* a specific option */
	for (guint i = 0; i < self->filenames->len; i++) {
		const gchar *fn = g_ptr_array_index(self->filenames, i);
		g_debug("trying to load config values from %s", fn);
		if (g_file_test(fn, G_FILE_TEST_EXISTS)) {
			g_autoptr(GBytes) blob = fu_common_get_contents_bytes(fn, error);
			if (blob == NULL)
				return FALSE;
			fu_byte_array_append_bytes(buf, blob);
		}
	}

	/* load if either file found */
	if (buf->len > 0) {
		if (!g_key_file_load_from_data(keyfile,
					       (const gchar *)buf->data,
					       buf->len,
					       G_KEY_FILE_NONE,
					       error))
			return FALSE;
	}

	/* get disabled devices */
	g_ptr_array_set_size(self->disabled_devices, 0);
	devices = g_key_file_get_string_list(keyfile,
					     "fwupd",
					     "DisabledDevices",
					     NULL, /* length */
					     NULL);
	if (devices != NULL) {
		for (guint i = 0; devices[i] != NULL; i++) {
			g_ptr_array_add(self->disabled_devices, g_strdup(devices[i]));
		}
	}

	/* get disabled plugins */
	g_ptr_array_set_size(self->disabled_plugins, 0);
	plugins = g_key_file_get_string_list(keyfile,
					     "fwupd",
					     "DisabledPlugins",
					     NULL, /* length */
					     NULL);
	if (plugins != NULL) {
		for (guint i = 0; plugins[i] != NULL; i++) {
			g_ptr_array_add(self->disabled_plugins, g_strdup(plugins[i]));
		}
	}

	/* get approved firmware */
	g_ptr_array_set_size(self->approved_firmware, 0);
	approved_firmware = g_key_file_get_string_list(keyfile,
						       "fwupd",
						       "ApprovedFirmware",
						       NULL, /* length */
						       NULL);
	if (approved_firmware != NULL) {
		for (guint i = 0; approved_firmware[i] != NULL; i++) {
			g_ptr_array_add(self->approved_firmware, g_strdup(approved_firmware[i]));
		}
	}

	/* get blocked firmware */
	g_ptr_array_set_size(self->blocked_firmware, 0);
	blocked_firmware = g_key_file_get_string_list(keyfile,
						      "fwupd",
						      "BlockedFirmware",
						      NULL, /* length */
						      NULL);
	if (blocked_firmware != NULL) {
		for (guint i = 0; blocked_firmware[i] != NULL; i++) {
			g_ptr_array_add(self->blocked_firmware, g_strdup(blocked_firmware[i]));
		}
	}

	/* get download schemes */
	g_ptr_array_set_size(self->uri_schemes, 0);
	uri_schemes = g_key_file_get_string_list(keyfile,
						 "fwupd",
						 "UriSchemes",
						 NULL, /* length */
						 NULL);
	if (uri_schemes != NULL) {
		for (guint i = 0; uri_schemes[i] != NULL; i++) {
			g_ptr_array_add(self->uri_schemes, g_strdup(uri_schemes[i]));
		}
	}
	if (self->uri_schemes->len == 0) {
		g_ptr_array_add(self->uri_schemes, g_strdup("file"));
		g_ptr_array_add(self->uri_schemes, g_strdup("https"));
		g_ptr_array_add(self->uri_schemes, g_strdup("http"));
		g_ptr_array_add(self->uri_schemes, g_strdup("ipfs"));
	}

	/* get maximum archive size, defaulting to something sane */
	archive_size_max = g_key_file_get_uint64(keyfile, "fwupd", "ArchiveSizeMax", NULL);
	if (archive_size_max > 0) {
		self->archive_size_max = archive_size_max * 0x100000;
	} else {
		guint64 memory_size = fu_common_get_memory_size();
		g_autofree gchar *str = NULL;
		if (memory_size > 0) {
			self->archive_size_max = MAX(memory_size / 4, G_MAXSIZE);
			str = g_format_size(self->archive_size_max);
			g_debug("using autodetected max archive size %s", str);
		} else {
			self->archive_size_max = 512 * 0x100000;
			str = g_format_size(self->archive_size_max);
			g_debug("using fallback max archive size %s", str);
		}
	}

	/* get idle timeout */
	idle_timeout = g_key_file_get_uint64(keyfile, "fwupd", "IdleTimeout", NULL);
	if (idle_timeout > 0)
		self->idle_timeout = idle_timeout;

	/* get the domains to run in verbose */
	domains = g_key_file_get_string(keyfile, "fwupd", "VerboseDomains", NULL);
	if (domains != NULL && domains[0] != '\0')
		g_setenv("FWUPD_VERBOSE", domains, TRUE);

	/* whether to update the motd on changes */
	self->update_motd =
	    g_key_file_get_boolean(keyfile, "fwupd", "UpdateMotd", &error_update_motd);
	if (!self->update_motd && error_update_motd != NULL) {
		g_debug("failed to read UpdateMotd key: %s", error_update_motd->message);
		self->update_motd = TRUE;
	}

	/* whether to only show supported devices for some plugins */
	self->enumerate_all_devices =
	    g_key_file_get_boolean(keyfile, "fwupd", "EnumerateAllDevices", &error_enumerate_all);
	/* if error parsing or missing, we want to default to true */
	if (!self->enumerate_all_devices && error_enumerate_all != NULL) {
		g_debug("failed to read EnumerateAllDevices key: %s", error_enumerate_all->message);
		self->enumerate_all_devices = TRUE;
	}

	/* whether to ignore power levels for updates */
	self->ignore_power =
	    g_key_file_get_boolean(keyfile, "fwupd", "IgnorePower", &error_ignore_power);
	if (!self->ignore_power && error_ignore_power != NULL) {
		g_debug("failed to read IgnorePower key: %s", error_ignore_power->message);
		self->ignore_power = FALSE;
	}

	/* whether to allow untrusted firmware *at all* even with PolicyKit auth */
	self->only_trusted =
	    g_key_file_get_boolean(keyfile, "fwupd", "OnlyTrusted", &error_only_trusted);
	if (!self->only_trusted && error_only_trusted != NULL) {
		g_debug("failed to read OnlyTrusted key: %s", error_only_trusted->message);
		self->only_trusted = TRUE;
	}

	/* fetch host best known configuration */
	host_bkc = g_key_file_get_string(keyfile, "fwupd", "HostBkc", NULL);
	if (host_bkc != NULL && host_bkc[0] != '\0')
		self->host_bkc = g_steal_pointer(&host_bkc);

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
	g_debug("%s changed, reloading all configs", fn);
	if (!fu_config_reload(self, &error))
		g_warning("failed to rescan daemon config: %s", error->message);
	fu_config_emit_changed(self);
}

gboolean
fu_config_set_key_value(FuConfig *self, const gchar *key, const gchar *value, GError **error)
{
	g_autoptr(GKeyFile) keyfile = g_key_file_new();
	const gchar *fn;

	/* sanity check */
	if (self->filenames->len == 0) {
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_INITIALIZED, "no config to load");
		return FALSE;
	}

	/* only write the file in /etc */
	fn = g_ptr_array_index(self->filenames, 0);
	if (!g_key_file_load_from_file(keyfile, fn, G_KEY_FILE_KEEP_COMMENTS, error))
		return FALSE;
	g_key_file_set_string(keyfile, "fwupd", key, value);
	if (!g_key_file_save_to_file(keyfile, fn, error))
		return FALSE;

	return fu_config_reload(self, error);
}

gboolean
fu_config_load(FuConfig *self, GError **error)
{
	g_autofree gchar *configdir_mut = fu_common_get_path(FU_PATH_KIND_LOCALCONFDIR_PKG);
	g_autofree gchar *configdir = fu_common_get_path(FU_PATH_KIND_SYSCONFDIR_PKG);

	g_return_val_if_fail(FU_IS_CONFIG(self), FALSE);
	g_return_val_if_fail(self->filenames->len == 0, FALSE);

	/* load the main daemon config file */
	g_ptr_array_add(self->filenames, g_build_filename(configdir, "daemon.conf", NULL));
	g_ptr_array_add(self->filenames, g_build_filename(configdir_mut, "daemon.conf", NULL));
	if (!fu_config_reload(self, error))
		return FALSE;

	/* set up a notify watches */
	for (guint i = 0; i < self->filenames->len; i++) {
		const gchar *fn = g_ptr_array_index(self->filenames, i);
		g_autoptr(GFile) file = g_file_new_for_path(fn);
		g_autoptr(GFileMonitor) monitor = NULL;

		monitor = g_file_monitor(file, G_FILE_MONITOR_NONE, NULL, error);
		if (monitor == NULL)
			return FALSE;
		g_signal_connect(G_FILE_MONITOR(monitor),
				 "changed",
				 G_CALLBACK(fu_config_monitor_changed_cb),
				 self);
		g_ptr_array_add(self->monitors, g_steal_pointer(&monitor));
	}

	/* success */
	return TRUE;
}

guint
fu_config_get_idle_timeout(FuConfig *self)
{
	g_return_val_if_fail(FU_IS_CONFIG(self), 0);
	return self->idle_timeout;
}

GPtrArray *
fu_config_get_disabled_devices(FuConfig *self)
{
	g_return_val_if_fail(FU_IS_CONFIG(self), NULL);
	return self->disabled_devices;
}

GPtrArray *
fu_config_get_blocked_firmware(FuConfig *self)
{
	g_return_val_if_fail(FU_IS_CONFIG(self), NULL);
	return self->blocked_firmware;
}

guint
fu_config_get_uri_scheme_prio(FuConfig *self, const gchar *scheme)
{
#if GLIB_CHECK_VERSION(2, 54, 0)
	guint idx = G_MAXUINT;
	g_ptr_array_find_with_equal_func(self->uri_schemes, scheme, g_str_equal, &idx);
	return idx;
#else
	for (guint i = 0; i < self->uri_schemes->len; i++)
		const gchar *scheme_tmp = g_ptr_array_index(self->uri_schemes, i);
	if (g_str_equal(scheme_tmp, scheme))
		return i;
}
return G_MAXUINT;
#endif
}

guint64
fu_config_get_archive_size_max(FuConfig *self)
{
	g_return_val_if_fail(FU_IS_CONFIG(self), 0);
	return self->archive_size_max;
}

GPtrArray *
fu_config_get_disabled_plugins(FuConfig *self)
{
	g_return_val_if_fail(FU_IS_CONFIG(self), NULL);
	return self->disabled_plugins;
}

GPtrArray *
fu_config_get_approved_firmware(FuConfig *self)
{
	g_return_val_if_fail(FU_IS_CONFIG(self), NULL);
	return self->approved_firmware;
}

gboolean
fu_config_get_update_motd(FuConfig *self)
{
	g_return_val_if_fail(FU_IS_CONFIG(self), FALSE);
	return self->update_motd;
}

gboolean
fu_config_get_ignore_power(FuConfig *self)
{
	g_return_val_if_fail(FU_IS_CONFIG(self), FALSE);
	return self->ignore_power;
}

gboolean
fu_config_get_only_trusted(FuConfig *self)
{
	g_return_val_if_fail(FU_IS_CONFIG(self), FALSE);
	return self->only_trusted;
}

gboolean
fu_config_get_enumerate_all_devices(FuConfig *self)
{
	g_return_val_if_fail(FU_IS_CONFIG(self), FALSE);
	return self->enumerate_all_devices;
}

const gchar *
fu_config_get_host_bkc(FuConfig *self)
{
	g_return_val_if_fail(FU_IS_CONFIG(self), NULL);
	return self->host_bkc;
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
}

static void
fu_config_init(FuConfig *self)
{
	self->filenames = g_ptr_array_new_with_free_func(g_free);
	self->disabled_devices = g_ptr_array_new_with_free_func(g_free);
	self->disabled_plugins = g_ptr_array_new_with_free_func(g_free);
	self->approved_firmware = g_ptr_array_new_with_free_func(g_free);
	self->blocked_firmware = g_ptr_array_new_with_free_func(g_free);
	self->uri_schemes = g_ptr_array_new_with_free_func(g_free);
	self->monitors = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
}

static void
fu_config_finalize(GObject *obj)
{
	FuConfig *self = FU_CONFIG(obj);

	for (guint i = 0; i < self->monitors->len; i++) {
		GFileMonitor *monitor = g_ptr_array_index(self->monitors, i);
		g_file_monitor_cancel(monitor);
	}
	g_ptr_array_unref(self->filenames);
	g_ptr_array_unref(self->monitors);
	g_ptr_array_unref(self->disabled_devices);
	g_ptr_array_unref(self->disabled_plugins);
	g_ptr_array_unref(self->approved_firmware);
	g_ptr_array_unref(self->blocked_firmware);
	g_ptr_array_unref(self->uri_schemes);
	g_free(self->host_bkc);

	G_OBJECT_CLASS(fu_config_parent_class)->finalize(obj);
}

FuConfig *
fu_config_new(void)
{
	FuConfig *self;
	self = g_object_new(FU_TYPE_CONFIG, NULL);
	return FU_CONFIG(self);
}

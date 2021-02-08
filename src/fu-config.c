/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuConfig"

#include "config.h"

#include <glib-object.h>
#include <gio/gio.h>

#include "fu-common.h"
#include "fu-config.h"

enum {
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

static void fu_config_finalize	 (GObject *obj);

struct _FuConfig
{
	GObject			 parent_instance;
	GFileMonitor		*monitor;
	GPtrArray		*disabled_devices;	/* (element-type utf-8) */
	GPtrArray		*disabled_plugins;	/* (element-type utf-8) */
	GPtrArray		*approved_firmware;	/* (element-type utf-8) */
	GPtrArray		*blocked_firmware;	/* (element-type utf-8) */
	GPtrArray		*uri_schemes;		/* (element-type utf-8) */
	guint64			 archive_size_max;
	guint			 idle_timeout;
	gchar			*config_file;
	gboolean		 update_motd;
	gboolean		 enumerate_all_devices;
};

G_DEFINE_TYPE (FuConfig, fu_config, G_TYPE_OBJECT)

static void
fu_config_emit_changed (FuConfig *self)
{
	g_debug ("::configuration changed");
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static gboolean
fu_config_reload (FuConfig *self, GError **error)
{
	guint64 archive_size_max;
	guint idle_timeout;
	g_auto(GStrv) approved_firmware = NULL;
	g_auto(GStrv) blocked_firmware = NULL;
	g_auto(GStrv) uri_schemes = NULL;
	g_auto(GStrv) devices = NULL;
	g_auto(GStrv) plugins = NULL;
	g_autofree gchar *domains = NULL;
	g_autoptr(GKeyFile) keyfile = g_key_file_new ();
	g_autoptr(GError) error_update_motd = NULL;
	g_autoptr(GError) error_enumerate_all = NULL;

	g_debug ("loading config values from %s", self->config_file);
	if (!g_key_file_load_from_file (keyfile, self->config_file,
					G_KEY_FILE_NONE, error))
		return FALSE;

	/* get disabled devices */
	g_ptr_array_set_size (self->disabled_devices, 0);
	devices = g_key_file_get_string_list (keyfile,
					      "fwupd",
					      "DisabledDevices",
					      NULL, /* length */
					      NULL);
	if (devices != NULL) {
		for (guint i = 0; devices[i] != NULL; i++) {
			g_ptr_array_add (self->disabled_devices,
					 g_strdup (devices[i]));
		}
	}

	/* get disabled plugins */
	g_ptr_array_set_size (self->disabled_plugins, 0);
	plugins = g_key_file_get_string_list (keyfile,
					      "fwupd",
					      "DisabledPlugins",
					      NULL, /* length */
					      NULL);
	if (plugins != NULL) {
		for (guint i = 0; plugins[i] != NULL; i++) {
			g_ptr_array_add (self->disabled_plugins,
					 g_strdup (plugins[i]));
		}
	}

	/* get approved firmware */
	g_ptr_array_set_size (self->approved_firmware, 0);
	approved_firmware = g_key_file_get_string_list (keyfile,
							"fwupd",
							"ApprovedFirmware",
							NULL, /* length */
							NULL);
	if (approved_firmware != NULL) {
		for (guint i = 0; approved_firmware[i] != NULL; i++) {
			g_ptr_array_add (self->approved_firmware,
					 g_strdup (approved_firmware[i]));
		}
	}

	/* get blocked firmware */
	g_ptr_array_set_size (self->blocked_firmware, 0);
	blocked_firmware = g_key_file_get_string_list (keyfile,
						       "fwupd",
						       "BlockedFirmware",
						       NULL, /* length */
						       NULL);
	if (blocked_firmware != NULL) {
		for (guint i = 0; blocked_firmware[i] != NULL; i++) {
			g_ptr_array_add (self->blocked_firmware,
					 g_strdup (blocked_firmware[i]));
		}
	}

	/* get download schemes */
	g_ptr_array_set_size (self->uri_schemes, 0);
	uri_schemes = g_key_file_get_string_list (keyfile,
						  "fwupd",
						  "UriSchemes",
						  NULL, /* length */
						  NULL);
	if (uri_schemes != NULL) {
		for (guint i = 0; uri_schemes[i] != NULL; i++) {
			g_ptr_array_add (self->uri_schemes,
					 g_strdup (uri_schemes[i]));
		}
	}
	if (self->uri_schemes->len == 0) {
		g_ptr_array_add (self->uri_schemes, g_strdup ("file"));
		g_ptr_array_add (self->uri_schemes, g_strdup ("https"));
		g_ptr_array_add (self->uri_schemes, g_strdup ("http"));
		g_ptr_array_add (self->uri_schemes, g_strdup ("ipfs"));
	}

	/* get maximum archive size, defaulting to something sane */
	archive_size_max = g_key_file_get_uint64 (keyfile,
						  "fwupd",
						  "ArchiveSizeMax",
						  NULL);
	if (archive_size_max > 0) {
		self->archive_size_max = archive_size_max * 0x100000;
	} else {
		guint64 memory_size = fu_common_get_memory_size ();
		g_autofree gchar *str = NULL;
		if (memory_size > 0) {
			self->archive_size_max = MAX (memory_size / 4, G_MAXSIZE);
			str = g_format_size (self->archive_size_max);
			g_debug ("using autodetected max archive size %s", str);
		} else {
			self->archive_size_max = 512 * 0x100000;
			str = g_format_size (self->archive_size_max);
			g_debug ("using fallback max archive size %s", str);
		}
	}

	/* get idle timeout */
	idle_timeout = g_key_file_get_uint64 (keyfile,
					      "fwupd",
					      "IdleTimeout",
					      NULL);
	if (idle_timeout > 0)
		self->idle_timeout = idle_timeout;

	/* get the domains to run in verbose */
	domains = g_key_file_get_string (keyfile,
					 "fwupd",
					 "VerboseDomains",
					 NULL);
	if (domains != NULL && domains[0] != '\0')
		g_setenv ("FWUPD_VERBOSE", domains, TRUE);

	/* whether to update the motd on changes */
	self->update_motd = g_key_file_get_boolean (keyfile,
						   "fwupd",
						   "UpdateMotd",
						   &error_update_motd);
	if (!self->update_motd && error_update_motd != NULL) {
		g_debug ("failed to read UpdateMotd key: %s", error_update_motd->message);
		self->update_motd = TRUE;
	}

	/* whether to only show supported devices for some plugins */
	self->enumerate_all_devices = g_key_file_get_boolean (keyfile,
							      "fwupd",
							      "EnumerateAllDevices",
							      &error_enumerate_all);
	/* if error parsing or missing, we want to default to true */
	if (!self->enumerate_all_devices && error_enumerate_all != NULL) {
		g_debug ("failed to read EnumerateAllDevices key: %s", error_enumerate_all->message);
		self->enumerate_all_devices = TRUE;
	}

	return TRUE;
}

static void
fu_config_monitor_changed_cb (GFileMonitor *monitor,
			      GFile *file,
			      GFile *other_file,
			      GFileMonitorEvent event_type,
			      gpointer user_data)
{
	FuConfig *self = FU_CONFIG (user_data);
	g_autoptr(GError) error = NULL;
	g_debug ("%s changed, reloading all configs", self->config_file);
	if (!fu_config_reload (self, &error))
		g_warning ("failed to rescan daemon config: %s", error->message);
	fu_config_emit_changed (self);
}

gboolean
fu_config_set_key_value (FuConfig *self, const gchar *key, const gchar *value, GError **error)
{
	g_autoptr(GKeyFile) keyfile = g_key_file_new ();
	if (!g_key_file_load_from_file (keyfile, self->config_file,
					G_KEY_FILE_KEEP_COMMENTS, error))
		return FALSE;
	g_key_file_set_string (keyfile, "fwupd", key, value);
	if (!g_key_file_save_to_file (keyfile, self->config_file, error))
		return FALSE;
	return fu_config_reload (self, error);
}

gboolean
fu_config_load (FuConfig *self, GError **error)
{
	g_autofree gchar *configdir = NULL;
	g_autoptr(GFile) file = NULL;

	g_return_val_if_fail (FU_IS_CONFIG (self), FALSE);
	g_return_val_if_fail (self->config_file == NULL, FALSE);

	/* load the main daemon config file */
	configdir = fu_common_get_path (FU_PATH_KIND_SYSCONFDIR_PKG);
	self->config_file = g_build_filename (configdir, "daemon.conf", NULL);
	if (g_file_test (self->config_file, G_FILE_TEST_EXISTS)) {
		if (!fu_config_reload (self, error))
			return FALSE;
	} else {
		g_warning ("Daemon configuration %s not found", self->config_file);
	}

	/* set up a notify watch */
	file = g_file_new_for_path (self->config_file);
	self->monitor = g_file_monitor (file, G_FILE_MONITOR_NONE, NULL, error);
	if (self->monitor == NULL)
		return FALSE;
	g_signal_connect (self->monitor, "changed",
			  G_CALLBACK (fu_config_monitor_changed_cb), self);

	/* success */
	return TRUE;
}

guint
fu_config_get_idle_timeout (FuConfig *self)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), 0);
	return self->idle_timeout;
}

GPtrArray *
fu_config_get_disabled_devices (FuConfig *self)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), NULL);
	return self->disabled_devices;
}

GPtrArray *
fu_config_get_blocked_firmware (FuConfig *self)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), NULL);
	return self->blocked_firmware;
}

guint
fu_config_get_uri_scheme_prio (FuConfig *self, const gchar *scheme)
{
#if GLIB_CHECK_VERSION(2,54,0)
	guint idx = G_MAXUINT;
	g_ptr_array_find_with_equal_func (self->uri_schemes,
					  scheme, g_str_equal, &idx);
	return idx;
#else
	for (guint i = 0; i < self->uri_schemes->len; i++)
		const gchar *scheme_tmp = g_ptr_array_index (self->uri_schemes, i);
		if (g_str_equal (scheme_tmp, scheme))
			return i;
	}
	return G_MAXUINT;
#endif
}

guint64
fu_config_get_archive_size_max (FuConfig *self)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), 0);
	return self->archive_size_max;
}

GPtrArray *
fu_config_get_disabled_plugins (FuConfig *self)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), NULL);
	return self->disabled_plugins;
}

GPtrArray *
fu_config_get_approved_firmware (FuConfig *self)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), NULL);
	return self->approved_firmware;
}

gboolean
fu_config_get_update_motd (FuConfig *self)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), FALSE);
	return self->update_motd;
}

gboolean
fu_config_get_enumerate_all_devices (FuConfig *self)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), FALSE);
	return self->enumerate_all_devices;
}

static void
fu_config_class_init (FuConfigClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_config_finalize;

	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

static void
fu_config_init (FuConfig *self)
{
	self->disabled_devices = g_ptr_array_new_with_free_func (g_free);
	self->disabled_plugins = g_ptr_array_new_with_free_func (g_free);
	self->approved_firmware = g_ptr_array_new_with_free_func (g_free);
	self->blocked_firmware = g_ptr_array_new_with_free_func (g_free);
	self->uri_schemes = g_ptr_array_new_with_free_func (g_free);
}

static void
fu_config_finalize (GObject *obj)
{
	FuConfig *self = FU_CONFIG (obj);

	if (self->monitor != NULL) {
		g_file_monitor_cancel (self->monitor);
		g_object_unref (self->monitor);
	}
	g_ptr_array_unref (self->disabled_devices);
	g_ptr_array_unref (self->disabled_plugins);
	g_ptr_array_unref (self->approved_firmware);
	g_ptr_array_unref (self->blocked_firmware);
	g_ptr_array_unref (self->uri_schemes);
	g_free (self->config_file);

	G_OBJECT_CLASS (fu_config_parent_class)->finalize (obj);
}

FuConfig *
fu_config_new (void)
{
	FuConfig *self;
	self = g_object_new (FU_TYPE_CONFIG, NULL);
	return FU_CONFIG (self);
}

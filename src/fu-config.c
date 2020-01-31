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
	GPtrArray		*blacklist_devices;	/* (element-type utf-8) */
	GPtrArray		*blacklist_plugins;	/* (element-type utf-8) */
	GPtrArray		*approved_firmware;	/* (element-type utf-8) */
	guint64			 archive_size_max;
	guint			 idle_timeout;
	gchar			*config_file;
	gboolean		 update_motd;
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
	g_auto(GStrv) devices = NULL;
	g_auto(GStrv) plugins = NULL;
	g_autofree gchar *domains = NULL;
	g_autoptr(GKeyFile) keyfile = g_key_file_new ();

	g_debug ("loading config values from %s", self->config_file);
	if (!g_key_file_load_from_file (keyfile, self->config_file,
					G_KEY_FILE_NONE, error))
		return FALSE;

	/* get blacklisted devices */
	g_ptr_array_set_size (self->blacklist_devices, 0);
	devices = g_key_file_get_string_list (keyfile,
					      "fwupd",
					      "BlacklistDevices",
					      NULL, /* length */
					      NULL);
	if (devices != NULL) {
		for (guint i = 0; devices[i] != NULL; i++) {
			g_ptr_array_add (self->blacklist_devices,
					 g_strdup (devices[i]));
		}
	}

	/* get blacklisted plugins */
	g_ptr_array_set_size (self->blacklist_plugins, 0);
	plugins = g_key_file_get_string_list (keyfile,
					      "fwupd",
					      "BlacklistPlugins",
					      NULL, /* length */
					      NULL);
	if (plugins != NULL) {
		for (guint i = 0; plugins[i] != NULL; i++) {
			g_ptr_array_add (self->blacklist_plugins,
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

	/* get maximum archive size, defaulting to something sane */
	archive_size_max = g_key_file_get_uint64 (keyfile,
						  "fwupd",
						  "ArchiveSizeMax",
						  NULL);
	if (archive_size_max > 0)
		self->archive_size_max = archive_size_max *= 0x100000;

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
						   NULL);

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
fu_config_get_blacklist_devices (FuConfig *self)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), NULL);
	return self->blacklist_devices;
}

guint64
fu_config_get_archive_size_max (FuConfig *self)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), 0);
	return self->archive_size_max;
}

GPtrArray *
fu_config_get_blacklist_plugins (FuConfig *self)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), NULL);
	return self->blacklist_plugins;
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
	self->archive_size_max = 512 * 0x100000;
	self->blacklist_devices = g_ptr_array_new_with_free_func (g_free);
	self->blacklist_plugins = g_ptr_array_new_with_free_func (g_free);
	self->approved_firmware = g_ptr_array_new_with_free_func (g_free);
}

static void
fu_config_finalize (GObject *obj)
{
	FuConfig *self = FU_CONFIG (obj);

	if (self->monitor != NULL)
		g_object_unref (self->monitor);
	g_ptr_array_unref (self->blacklist_devices);
	g_ptr_array_unref (self->blacklist_plugins);
	g_ptr_array_unref (self->approved_firmware);
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

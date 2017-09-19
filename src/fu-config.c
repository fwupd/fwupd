/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "config.h"

#include <glib-object.h>
#include <gio/gio.h>

#include "fu-config.h"

#include "fwupd-error.h"
#include "fwupd-remote-private.h"

static void fu_config_finalize	 (GObject *obj);

struct _FuConfig
{
	GObject			 parent_instance;
	GKeyFile		*keyfile;
	GPtrArray		*remotes;
	GPtrArray		*monitors;
	GPtrArray		*blacklist_devices;
	GPtrArray		*blacklist_plugins;
};

G_DEFINE_TYPE (FuConfig, fu_config, G_TYPE_OBJECT)

static GPtrArray *
fu_config_get_config_paths (void)
{
	GPtrArray *paths = g_ptr_array_new_with_free_func (g_free);
	const gchar *remotes_dir;
	const gchar *system_prefixlibdir = "/usr/lib/fwupd";
	g_autofree gchar *sysconfdir = NULL;

	/* only set by the self test program */
	remotes_dir = g_getenv ("FU_SELF_TEST_REMOTES_DIR");
	if (remotes_dir != NULL) {
		g_ptr_array_add (paths, g_strdup (remotes_dir));
		return paths;
	}

	/* use sysconfig, and then fall back to /etc */
	sysconfdir = g_build_filename (FWUPDCONFIGDIR, NULL);
	if (g_file_test (sysconfdir, G_FILE_TEST_EXISTS))
		g_ptr_array_add (paths, g_steal_pointer (&sysconfdir));

	/* add in system-wide locations */
	if (g_file_test (system_prefixlibdir, G_FILE_TEST_EXISTS))
		g_ptr_array_add (paths, g_strdup (system_prefixlibdir));

	return paths;
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
	g_autofree gchar *filename = g_file_get_path (file);
	g_debug ("%s changed, reloading all configs", filename);
	if (!fu_config_load (self, &error))
		g_warning ("failed to rescan config: %s", error->message);
}

static guint64
fu_config_get_remote_mtime (FuConfig *self, FwupdRemote *remote)
{
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;

	file = g_file_new_for_path (fwupd_remote_get_filename_cache (remote));
	if (!g_file_query_exists (file, NULL))
		return G_MAXUINT64;
	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_TIME_MODIFIED,
				  G_FILE_QUERY_INFO_NONE,
				  NULL, NULL);
	if (info == NULL)
		return G_MAXUINT64;
	return g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
}

static gboolean
fu_config_add_inotify (FuConfig *self, const gchar *filename, GError **error)
{
	GFileMonitor *monitor;
	g_autoptr(GFile) file = g_file_new_for_path (filename);

	/* set up a notify watch */
	monitor = g_file_monitor (file, G_FILE_MONITOR_NONE, NULL, error);
	if (monitor == NULL)
		return FALSE;
	g_signal_connect (monitor, "changed",
			  G_CALLBACK (fu_config_monitor_changed_cb), self);
	g_ptr_array_add (self->monitors, monitor);
	return TRUE;
}

static gboolean
fu_config_add_remotes_for_path (FuConfig *self, const gchar *path, GError **error)
{
	const gchar *tmp;
	g_autofree gchar *path_remotes = NULL;
	g_autoptr(GDir) dir = NULL;

	path_remotes = g_build_filename (path, "remotes.d", NULL);
	if (!g_file_test (path_remotes, G_FILE_TEST_EXISTS))
		return TRUE;
	dir = g_dir_open (path_remotes, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((tmp = g_dir_read_name (dir)) != NULL) {
		g_autofree gchar *filename = g_build_filename (path_remotes, tmp, NULL);
		g_autoptr(FwupdRemote) remote = fwupd_remote_new ();

		/* skip invalid files */
		if (!g_str_has_suffix (tmp, ".conf")) {
			g_debug ("skipping invalid file %s", filename);
			continue;
		}

		/* load from keyfile */
		g_debug ("loading config from %s", filename);
		if (!fwupd_remote_load_from_filename (remote, filename,
						      NULL, error)) {
			g_prefix_error (error, "failed to load %s: ", filename);
			return FALSE;
		}

		/* watch the config file and the XML file itself */
		if (!fu_config_add_inotify (self, filename, error))
			return FALSE;
		if (!fu_config_add_inotify (self, fwupd_remote_get_filename_cache (remote), error))
			return FALSE;

		/* set mtime */
		fwupd_remote_set_mtime (remote, fu_config_get_remote_mtime (self, remote));
		g_ptr_array_add (self->remotes, g_steal_pointer (&remote));
	}
	return TRUE;
}

static gint
fu_config_remote_sort_cb (gconstpointer a, gconstpointer b)
{
	FwupdRemote *remote_a = *((FwupdRemote **) a);
	FwupdRemote *remote_b = *((FwupdRemote **) b);

	/* use priority first */
	if (fwupd_remote_get_priority (remote_a) < fwupd_remote_get_priority (remote_b))
		return 1;
	if (fwupd_remote_get_priority (remote_a) > fwupd_remote_get_priority (remote_b))
		return -1;

	/* fall back to name */
	return g_strcmp0 (fwupd_remote_get_id (remote_a),
			  fwupd_remote_get_id (remote_b));
}

static FwupdRemote *
fu_config_get_remote_by_id_noref (GPtrArray *remotes, const gchar *remote_id)
{
	for (guint i = 0; i < remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (remotes, i);
		if (g_strcmp0 (remote_id, fwupd_remote_get_id (remote)) == 0)
			return remote;
	}
	return NULL;
}

static guint
fu_config_remotes_depsolve_with_direction (FuConfig *self, gint inc)
{
	guint cnt = 0;
	for (guint i = 0; i < self->remotes->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (self->remotes, i);
		gchar **order = inc < 0 ? fwupd_remote_get_order_after (remote) :
					  fwupd_remote_get_order_before (remote);
		if (order == NULL)
			continue;
		for (guint j = 0; order[j] != NULL; j++) {
			FwupdRemote *remote2;
			if (g_strcmp0 (order[j], fwupd_remote_get_id (remote)) == 0) {
				g_debug ("ignoring self-dep remote %s", order[j]);
				continue;
			}
			remote2 = fu_config_get_remote_by_id_noref (self->remotes, order[j]);
			if (remote2 == NULL) {
				g_debug ("ignoring unfound remote %s", order[j]);
				continue;
			}
			if (fwupd_remote_get_priority (remote) > fwupd_remote_get_priority (remote2))
				continue;
			g_debug ("ordering %s=%s+%i",
				 fwupd_remote_get_id (remote),
				 fwupd_remote_get_id (remote2),
				 inc);
			fwupd_remote_set_priority (remote, fwupd_remote_get_priority (remote2) + inc);

			/* increment changes counter */
			cnt++;
		}
	}
	return cnt;
}

static gboolean
fu_config_load_remotes (FuConfig *self, GError **error)
{
	guint depsolve_check;
	g_autoptr(GPtrArray) paths = NULL;

	/* get a list of all config paths */
	paths = fu_config_get_config_paths ();
	if (paths->len == 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_NOT_FOUND,
				     "No search paths found");
		return FALSE;
	}

	/* look for all remotes */
	for (guint i = 0; i < paths->len; i++) {
		const gchar *path = g_ptr_array_index (paths, i);
		g_debug ("using config path of %s", path);
		if (!fu_config_add_remotes_for_path (self, path, error))
			return FALSE;
	}

	/* depsolve */
	for (depsolve_check = 0; depsolve_check < 100; depsolve_check++) {
		guint cnt = 0;
		cnt += fu_config_remotes_depsolve_with_direction (self, 1);
		cnt += fu_config_remotes_depsolve_with_direction (self, -1);
		if (cnt == 0)
			break;
	}
	if (depsolve_check == 100) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "Cannot depsolve remotes ordering");
		return FALSE;
	}

	/* order these by priority, then name */
	g_ptr_array_sort (self->remotes, fu_config_remote_sort_cb);

	/* success */
	return TRUE;
}

gboolean
fu_config_load (FuConfig *self, GError **error)
{
	GFileMonitor *monitor;
	g_autofree gchar *config_file = NULL;
	g_auto(GStrv) devices = NULL;
	g_auto(GStrv) plugins = NULL;
	g_autoptr(GFile) file = NULL;

	g_return_val_if_fail (FU_IS_CONFIG (self), FALSE);

	/* ensure empty in case we're called from a monitor change */
	g_ptr_array_set_size (self->blacklist_devices, 0);
	g_ptr_array_set_size (self->blacklist_plugins, 0);
	g_ptr_array_set_size (self->monitors, 0);
	g_ptr_array_set_size (self->remotes, 0);

	/* load the main daemon config file */
	config_file = g_build_filename (FWUPDCONFIGDIR, "daemon.conf", NULL);
	g_debug ("loading config values from %s", config_file);
	if (!g_key_file_load_from_file (self->keyfile, config_file,
					G_KEY_FILE_NONE, error))
		return FALSE;

	/* set up a notify watch */
	file = g_file_new_for_path (config_file);
	monitor = g_file_monitor (file, G_FILE_MONITOR_NONE, NULL, error);
	if (monitor == NULL)
		return FALSE;
	g_signal_connect (monitor, "changed",
			  G_CALLBACK (fu_config_monitor_changed_cb), self);
	g_ptr_array_add (self->monitors, monitor);

	/* get blacklisted devices */
	devices = g_key_file_get_string_list (self->keyfile,
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
	plugins = g_key_file_get_string_list (self->keyfile,
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

	/* load remotes */
	if (!fu_config_load_remotes (self, error))
		return FALSE;

	/* success */
	return TRUE;
}

GPtrArray *
fu_config_get_remotes (FuConfig *self)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), NULL);
	return self->remotes;
}

FwupdRemote *
fu_config_get_remote_by_id (FuConfig *self, const gchar *remote_id)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), NULL);
	return fu_config_get_remote_by_id_noref (self->remotes, remote_id);
}

GPtrArray *
fu_config_get_blacklist_devices (FuConfig *self)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), NULL);
	return self->blacklist_devices;
}

GPtrArray *
fu_config_get_blacklist_plugins (FuConfig *self)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), NULL);
	return self->blacklist_plugins;
}

static void
fu_config_class_init (FuConfigClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_config_finalize;
}

static void
fu_config_init (FuConfig *self)
{
	self->keyfile = g_key_file_new ();
	self->blacklist_devices = g_ptr_array_new_with_free_func (g_free);
	self->blacklist_plugins = g_ptr_array_new_with_free_func (g_free);
	self->remotes = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	self->monitors = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

static void
fu_config_finalize (GObject *obj)
{
	FuConfig *self = FU_CONFIG (obj);

	g_key_file_unref (self->keyfile);
	g_ptr_array_unref (self->blacklist_devices);
	g_ptr_array_unref (self->blacklist_plugins);
	g_ptr_array_unref (self->remotes);
	g_ptr_array_unref (self->monitors);

	G_OBJECT_CLASS (fu_config_parent_class)->finalize (obj);
}

FuConfig *
fu_config_new (void)
{
	FuConfig *self;
	self = g_object_new (FU_TYPE_CONFIG, NULL);
	return FU_CONFIG (self);
}

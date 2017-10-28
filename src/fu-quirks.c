/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <glib-object.h>
#include <gio/gio.h>
#include <fnmatch.h>
#include <string.h>

#include "fu-quirks.h"

#include "fwupd-error.h"
#include "fwupd-remote-private.h"

/**
 * SECTION:fu-quirks
 * @short_description: device quirks
 *
 * Quirks can be used to modify device behaviour.
 * When fwupd is installed in long-term support distros it's very hard to
 * backport new versions as new hardware is released.
 *
 * There are several reasons why we can't just include the mapping and quirk
 * information in the AppStream metadata:
 *
 * * The extra data is hugely specific to the installed fwupd plugin versions
 * * The device-id is per-device, and the mapping is usually per-plugin
 * * Often the information is needed before the FuDevice is created
 * * There are security implications in allowing plugins to handle new devices
 *
 * The idea with quirks is that the end user can drop an additional (or replace
 * an existing) file in a .d director with a simple format and the hardware will
 * magically start working. This assumes no new quirks are required, as this would
 * obviously need code changes, but allows us to get most existing devices working
 * in an easy way without the user compiling anything.
 *
 * See also: #FuDevice, #FuPlugin
 */

static void fu_quirks_finalize	 (GObject *obj);

struct _FuQuirks
{
	GObject			 parent_instance;
	GPtrArray		*monitors;
	GHashTable		*hash;	/* of prefix/id:string */
};

G_DEFINE_TYPE (FuQuirks, fu_quirks, G_TYPE_OBJECT)

static void
fu_quirks_monitor_changed_cb (GFileMonitor *monitor,
			      GFile *file,
			      GFile *other_file,
			      GFileMonitorEvent event_type,
			      gpointer user_data)
{
	FuQuirks *self = FU_QUIRKS (user_data);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *filename = g_file_get_path (file);
	g_debug ("%s changed, reloading all configs", filename);
	if (!fu_quirks_load (self, &error))
		g_warning ("failed to rescan quirks: %s", error->message);
}

static gboolean
fu_quirks_add_inotify (FuQuirks *self, const gchar *filename, GError **error)
{
	GFileMonitor *monitor;
	g_autoptr(GFile) file = g_file_new_for_path (filename);

	/* set up a notify watch */
	monitor = g_file_monitor (file, G_FILE_MONITOR_NONE, NULL, error);
	if (monitor == NULL)
		return FALSE;
	g_signal_connect (monitor, "changed",
			  G_CALLBACK (fu_quirks_monitor_changed_cb), self);
	g_ptr_array_add (self->monitors, monitor);
	return TRUE;
}

/**
 * fu_quirks_lookup_by_id:
 * @self: A #FuPlugin
 * @prefix: A string prefix that matches the quirks file basename, e.g. "dfu-quirks"
 * @id: An ID to match the entry, e.g. "012345"
 *
 * Looks up an entry in the hardware database using a string value.
 *
 * Returns: (transfer none): values from the database, or %NULL if not found
 *
 * Since: 1.0.1
 **/
const gchar *
fu_quirks_lookup_by_id (FuQuirks *self, const gchar *prefix, const gchar *id)
{
	g_autofree gchar *key = NULL;

	g_return_val_if_fail (FU_IS_QUIRKS (self), NULL);
	g_return_val_if_fail (prefix != NULL, NULL);
	g_return_val_if_fail (id != NULL, NULL);

	key = g_strdup_printf ("%s/%s", prefix, id);
	return g_hash_table_lookup (self->hash, key);
}

/**
 * fu_quirks_lookup_by_glob:
 * @self: A #FuPlugin
 * @prefix: A string prefix that matches the quirks file basename, e.g. "dfu-quirks"
 * @glob: An glob to match the entry, e.g. "foo*bar?baz"
 *
 * Looks up an entry in the hardware database using a key glob.
 * NOTE: This is *much* slower than using fu_quirks_lookup_by_id() as each key
 * in the quirk database is compared.
 *
 * Returns: (transfer none): values from the database, or %NULL if not found
 *
 * Since: 1.0.1
 **/
const gchar *
fu_quirks_lookup_by_glob (FuQuirks *self, const gchar *prefix, const gchar *glob)
{
	g_autoptr(GList) keys = NULL;
	gsize prefix_len;

	g_return_val_if_fail (FU_IS_QUIRKS (self), NULL);
	g_return_val_if_fail (prefix != NULL, NULL);
	g_return_val_if_fail (glob != NULL, NULL);

	prefix_len = strlen (prefix);
	keys = g_hash_table_get_keys (self->hash);
	for (GList *l = keys; l != NULL; l = l->next) {
		const gchar *id = l->data;
		if (strncmp (id, prefix, prefix_len) != 0)
			continue;
		id += prefix_len + 1;
		if (fnmatch (glob, id, 0) == 0)
			return fu_quirks_lookup_by_id (self, prefix, id);
		if (fnmatch (id, glob, 0) == 0)
			return fu_quirks_lookup_by_id (self, prefix, id);
	}
	return NULL;
}

/**
 * fu_quirks_lookup_by_usb_device:
 * @self: A #FuPlugin
 * @prefix: A string prefix that matches the quirks file basename, e.g. "dfu-quirks"
 * @dev: A #GUsbDevice
 *
 * Looks up an entry in the hardware database using various keys generated
 * from @dev.
 *
 * Returns: (transfer none): values from the database, or %NULL if not found
 *
 * Since: 1.0.1
 **/
const gchar *
fu_quirks_lookup_by_usb_device (FuQuirks *self, const gchar *prefix, GUsbDevice *dev)
{
	const gchar *tmp;
	g_autofree gchar *key1 = NULL;
	g_autofree gchar *key2 = NULL;
	g_autofree gchar *key3 = NULL;

	g_return_val_if_fail (FU_IS_QUIRKS (self), NULL);
	g_return_val_if_fail (prefix != NULL, NULL);
	g_return_val_if_fail (G_USB_IS_DEVICE (dev), NULL);

	/* prefer an exact match, VID:PID:REV */
	key1 = g_strdup_printf ("USB\\VID_%04X&PID_%04X&REV_%04X",
				g_usb_device_get_vid (dev),
				g_usb_device_get_pid (dev),
				g_usb_device_get_release (dev));
	tmp = fu_quirks_lookup_by_id (self, prefix, key1);
	if (tmp != NULL)
		return tmp;

	/* VID:PID */
	key2 = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
				g_usb_device_get_vid (dev),
				g_usb_device_get_pid (dev));
	tmp = fu_quirks_lookup_by_id (self, prefix, key2);
	if (tmp != NULL)
		return tmp;

	/* VID */
	key3 = g_strdup_printf ("USB\\VID_%04X", g_usb_device_get_vid (dev));
	return fu_quirks_lookup_by_id (self, prefix, key3);
}

static gboolean
fu_quirks_add_quirks_from_filename (FuQuirks *self, const gchar *filename, GError **error)
{
	g_autoptr(GKeyFile) kf = g_key_file_new ();
	g_auto(GStrv) groups = NULL;

	/* load keyfile */
	if (!g_key_file_load_from_file (kf, filename, G_KEY_FILE_NONE, error))
		return FALSE;

	/* add each set of groups and keys */
	groups = g_key_file_get_groups (kf, NULL);
	for (guint i = 0; groups[i] != NULL; i++) {
		g_auto(GStrv) keys = NULL;
		keys = g_key_file_get_keys (kf, groups[i], NULL, error);
		if (keys == NULL)
			return FALSE;
		for (guint j = 0; keys[j] != NULL; j++) {
			g_autofree gchar *tmp = NULL;
			tmp = g_key_file_get_string (kf, groups[i], keys[j], error);
			if (tmp == NULL)
				return FALSE;
			g_hash_table_insert (self->hash,
					     g_strdup_printf ("%s/%s", groups[i], keys[j]),
					     g_steal_pointer (&tmp));
		}
	}
	g_debug ("now %u quirk entries", g_hash_table_size (self->hash));
	return TRUE;
}

static gint
fu_quirks_filename_sort_cb (gconstpointer a, gconstpointer b)
{
	const gchar *stra = *((const gchar **) a);
	const gchar *strb = *((const gchar **) b);
	return g_strcmp0 (stra, strb);
}

static gboolean
fu_quirks_add_quirks_for_path (FuQuirks *self, const gchar *path, GError **error)
{
	const gchar *tmp;
	g_autofree gchar *path_hw = NULL;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GPtrArray) filenames = g_ptr_array_new_with_free_func (g_free);

	/* add valid files to the array */
	path_hw = g_build_filename (path, "quirks.d", NULL);
	if (!g_file_test (path_hw, G_FILE_TEST_EXISTS)) {
		g_debug ("no %s, skipping", path_hw);
		return TRUE;
	}
	dir = g_dir_open (path_hw, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((tmp = g_dir_read_name (dir)) != NULL) {
		if (!g_str_has_suffix (tmp, ".quirk")) {
			g_debug ("skipping invalid file %s", tmp);
			continue;
		}
		g_ptr_array_add (filenames, g_build_filename (path_hw, tmp, NULL));
	}

	/* sort */
	g_ptr_array_sort (filenames, fu_quirks_filename_sort_cb);

	/* process files */
	for (guint i = 0; i < filenames->len; i++) {
		const gchar *filename = g_ptr_array_index (filenames, i);

		/* load from keyfile */
		g_debug ("loading quirks from %s", filename);
		if (!fu_quirks_add_quirks_from_filename (self, filename, error)) {
			g_prefix_error (error, "failed to load %s: ", filename);
			return FALSE;
		}

		/* watch the file for changes */
		if (!fu_quirks_add_inotify (self, filename, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_quirks_load: (skip)
 * @self: A #FuQuirks
 * @error: A #GError, or %NULL
 *
 * Loads the various files that define the hardware quirks used in plugins.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.0.1
 **/
gboolean
fu_quirks_load (FuQuirks *self, GError **error)
{
	g_autofree gchar *localstate_fwupd = NULL;
	g_return_val_if_fail (FU_IS_QUIRKS (self), FALSE);

	/* ensure empty in case we're called from a monitor change */
	g_ptr_array_set_size (self->monitors, 0);
	g_hash_table_remove_all (self->hash);

	/* system datadir */
	if (!fu_quirks_add_quirks_for_path (self, FWUPDDATADIR, error))
		return FALSE;

	/* something we can write when using Ostree */
	localstate_fwupd = g_build_filename (LOCALSTATEDIR, "lib", "fwupd", NULL);
	if (!fu_quirks_add_quirks_for_path (self, localstate_fwupd, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_quirks_class_init (FuQuirksClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_quirks_finalize;
}

static void
fu_quirks_init (FuQuirks *self)
{
	self->monitors = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	self->hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
}

static void
fu_quirks_finalize (GObject *obj)
{
	FuQuirks *self = FU_QUIRKS (obj);
	g_ptr_array_unref (self->monitors);
	g_hash_table_unref (self->hash);
	G_OBJECT_CLASS (fu_quirks_parent_class)->finalize (obj);
}

/**
 * fu_quirks_new: (skip)
 *
 * Creates a new quirks object.
 *
 * Return value: a new #FuQuirks
 **/
FuQuirks *
fu_quirks_new (void)
{
	FuQuirks *self;
	self = g_object_new (FU_TYPE_QUIRKS, NULL);
	return FU_QUIRKS (self);
}

/*
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuQuirks"

#include "config.h"

#include <glib-object.h>
#include <gio/gio.h>
#include <string.h>

#include "fu-common.h"
#include "fu-common-guid.h"
#include "fu-mutex.h"
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
	GHashTable		*hash;	/* of group:{key:value} */
	FuMutex			*hash_mutex;
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

static gchar *
fu_quirks_build_group_key (const gchar *group)
{
	const gchar *guid_prefixes[] = { "DeviceInstanceId=", "Guid=", "HwId=", NULL };

	/* this is a GUID */
	for (guint i = 0; guid_prefixes[i] != NULL; i++) {
		if (g_str_has_prefix (group, guid_prefixes[i])) {
			gsize len = strlen (guid_prefixes[i]);
			if (fu_common_guid_is_valid (group + len))
				return g_strdup (group + len);
			return fu_common_guid_from_string (group + len);
		}
	}

	/* fallback */
	return g_strdup (group);
}

/**
 * fu_quirks_lookup_by_id:
 * @self: A #FuPlugin
 * @group: A string group, e.g. "DeviceInstanceId=USB\VID_1235&PID_AB11"
 * @key: An ID to match the entry, e.g. "Name"
 *
 * Looks up an entry in the hardware database using a string value.
 *
 * Returns: (transfer none): values from the database, or %NULL if not found
 *
 * Since: 1.0.1
 **/
const gchar *
fu_quirks_lookup_by_id (FuQuirks *self, const gchar *group, const gchar *key)
{
	GHashTable *kvs;
	g_autofree gchar *group_key = NULL;
	g_autoptr(FuMutexLocker) locker = fu_mutex_read_locker_new (self->hash_mutex);

	g_return_val_if_fail (FU_IS_QUIRKS (self), NULL);
	g_return_val_if_fail (group != NULL, NULL);
	g_return_val_if_fail (key != NULL, NULL);
	g_return_val_if_fail (locker != NULL, NULL);

	group_key = fu_quirks_build_group_key (group);
	kvs = g_hash_table_lookup (self->hash, group_key);
	if (kvs == NULL)
		return NULL;
	return g_hash_table_lookup (kvs, key);
}

/**
 * fu_quirks_get_kvs_for_guid:
 * @self: A #FuPlugin
 * @guid: a GUID
 * @iter: A #GHashTableIter, typically allocated on the stack by the caller
 *
 * Looks up all entries in the hardware database using a GUID value.
 *
 * Returns: %TRUE if the GUID was found, and @iter was set
 *
 * Since: 1.1.2
 **/
gboolean
fu_quirks_get_kvs_for_guid (FuQuirks *self, const gchar *guid, GHashTableIter *iter)
{
	GHashTable *kvs;
	g_autoptr(FuMutexLocker) locker = fu_mutex_read_locker_new (self->hash_mutex);
	g_return_val_if_fail (locker != NULL, FALSE);
	kvs = g_hash_table_lookup (self->hash, guid);
	if (kvs == NULL)
		return FALSE;
	g_hash_table_iter_init (iter, kvs);
	return TRUE;
}

static gchar *
fu_quirks_merge_values (const gchar *old, const gchar *new)
{
	guint cnt = 0;
	g_autofree gchar **resv = NULL;
	g_auto(GStrv) newv = g_strsplit (new, ",", -1);
	g_auto(GStrv) oldv = g_strsplit (old, ",", -1);

	/* segment flags, and append if they do not already exists */
	resv = g_new0 (gchar *, g_strv_length (oldv) + g_strv_length (newv) + 1);
	for (guint i = 0; oldv[i] != NULL; i++) {
		if (!g_strv_contains ((const gchar * const *) resv, oldv[i]))
			resv[cnt++] = oldv[i];
	}
	for (guint i = 0; newv[i] != NULL; i++) {
		if (!g_strv_contains ((const gchar * const *) resv, newv[i]))
			resv[cnt++] = newv[i];
	}
	return g_strjoinv (",", resv);
}

/**
 * fu_quirks_add_value: (skip)
 * @self: A #FuQuirks
 * @group: group, e.g. `DeviceInstanceId=USB\VID_0BDA&PID_1100`
 * @key: group, e.g. `Name`
 * @value: group, e.g. `Unknown Device`
 *
 * Adds a value to the quirk database. Normally this is achieved by loading a
 * quirk file using fu_quirks_load().
 *
 * Since: 1.1.2
 **/
void
fu_quirks_add_value (FuQuirks *self, const gchar *group, const gchar *key, const gchar *value)
{
	GHashTable *kvs;
	const gchar *value_old;
	g_autofree gchar *group_key = NULL;
	g_autofree gchar *value_new = NULL;
	g_autoptr(FuMutexLocker) locker = fu_mutex_write_locker_new (self->hash_mutex);

	g_return_if_fail (locker != NULL);

	/* does the key already exists in our hash */
	group_key = fu_quirks_build_group_key (group);
	kvs = g_hash_table_lookup (self->hash, group_key);
	if (kvs == NULL) {
		kvs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
		g_hash_table_insert (self->hash,
				     g_steal_pointer (&group_key),
				     kvs);
		value_new = g_strdup (value);
	} else {
		/* look up in the 2nd level hash */
		value_old = g_hash_table_lookup (kvs, key);
		if (value_old != NULL) {
			g_debug ("already found %s=%s, merging with %s",
				 group_key, value_old, value);
			value_new = fu_quirks_merge_values (value_old, value);
		} else {
			value_new = g_strdup (value);
		}
	}

	/* insert the new value */
	g_hash_table_insert (kvs, g_strdup (key), g_steal_pointer (&value_new));
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
			g_autofree gchar *value = NULL;
			/* get value from keyfile */
			value = g_key_file_get_value (kf, groups[i], keys[j], error);
			if (value == NULL)
				return FALSE;
			fu_quirks_add_value (self, groups[i], keys[j], value);
		}
	}
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
	g_debug ("now %u quirk entries", g_hash_table_size (self->hash));
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
	g_autofree gchar *datadir = NULL;
	g_autofree gchar *localstatedir = NULL;

	g_return_val_if_fail (FU_IS_QUIRKS (self), FALSE);

	/* ensure empty in case we're called from a monitor change */
	g_ptr_array_set_size (self->monitors, 0);
	fu_mutex_write_lock (self->hash_mutex);
	g_hash_table_remove_all (self->hash);
	fu_mutex_write_unlock (self->hash_mutex);

	/* system datadir */
	datadir = fu_common_get_path (FU_PATH_KIND_DATADIR_PKG);
	if (!fu_quirks_add_quirks_for_path (self, datadir, error))
		return FALSE;

	/* something we can write when using Ostree */
	localstatedir = fu_common_get_path (FU_PATH_KIND_LOCALSTATEDIR_PKG);
	if (!fu_quirks_add_quirks_for_path (self, localstatedir, error))
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
	self->hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_hash_table_unref);
	self->hash_mutex = fu_mutex_new (G_OBJECT_TYPE_NAME(self), "hash");
}

static void
fu_quirks_finalize (GObject *obj)
{
	FuQuirks *self = FU_QUIRKS (obj);
	g_ptr_array_unref (self->monitors);
	g_object_unref (self->hash_mutex);
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

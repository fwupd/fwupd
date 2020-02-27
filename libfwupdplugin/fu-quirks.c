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
#include <xmlb.h>

#include "fu-common.h"
#include "fu-mutex.h"
#include "fu-quirks.h"

#include "fwupd-common.h"
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
	FuQuirksLoadFlags	 load_flags;
	XbSilo			*silo;
};

G_DEFINE_TYPE (FuQuirks, fu_quirks, G_TYPE_OBJECT)

static gchar *
fu_quirks_build_group_key (const gchar *group)
{
	const gchar *guid_prefixes[] = { "DeviceInstanceId=", "Guid=", "HwId=", NULL };

	/* this is a GUID */
	for (guint i = 0; guid_prefixes[i] != NULL; i++) {
		if (g_str_has_prefix (group, guid_prefixes[i])) {
			gsize len = strlen (guid_prefixes[i]);
			if (fwupd_guid_is_valid (group + len))
				return g_strdup (group + len);
			return fwupd_guid_hash_string (group + len);
		}
	}

	/* fallback */
	return g_strdup (group);
}

static GInputStream *
fu_quirks_convert_quirk_to_xml_cb (XbBuilderSource *self,
				   XbBuilderSourceCtx *ctx,
				   gpointer user_data,
				   GCancellable *cancellable,
				   GError **error)
{
	g_autofree gchar *xml = NULL;
	g_auto(GStrv) groups = NULL;
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GKeyFile) kf = g_key_file_new ();
	g_autoptr(XbBuilderNode) root = xb_builder_node_new ("quirk");

	/* parse keyfile */
	bytes = xb_builder_source_ctx_get_bytes (ctx, cancellable, error);
	if (bytes == NULL)
		return NULL;
	if (!g_key_file_load_from_data (kf,
					g_bytes_get_data (bytes, NULL),
					g_bytes_get_size (bytes),
					G_KEY_FILE_NONE,
					error))
		return NULL;

	/* add each set of groups and keys */
	groups = g_key_file_get_groups (kf, NULL);
	for (guint i = 0; groups[i] != NULL; i++) {
		g_auto(GStrv) keys = NULL;
		g_autofree gchar *group_id = NULL;
		g_autoptr(XbBuilderNode) bn = NULL;
		keys = g_key_file_get_keys (kf, groups[i], NULL, error);
		if (keys == NULL)
			return NULL;
		group_id = fu_quirks_build_group_key (groups[i]);
		bn = xb_builder_node_insert (root, "device", "id", group_id, NULL);
		for (guint j = 0; keys[j] != NULL; j++) {
			g_autofree gchar *value = NULL;
			value = g_key_file_get_value (kf, groups[i], keys[j], error);
			if (value == NULL)
				return NULL;
			xb_builder_node_insert_text (bn,
						     "value", value,
						     "key", keys[j],
						     NULL);
		}
	}

	/* export as XML */
	xml = xb_builder_node_export (root, XB_NODE_EXPORT_FLAG_ADD_HEADER, error);
	if (xml == NULL)
		return NULL;
	return g_memory_input_stream_new_from_data (g_steal_pointer (&xml), -1, g_free);
}

static gint
fu_quirks_filename_sort_cb (gconstpointer a, gconstpointer b)
{
	const gchar *stra = *((const gchar **) a);
	const gchar *strb = *((const gchar **) b);
	return g_strcmp0 (stra, strb);
}

static gboolean
fu_quirks_add_quirks_for_path (FuQuirks *self, XbBuilder *builder,
			       const gchar *path, GError **error)
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
		g_autoptr(GFile) file = g_file_new_for_path (filename);
		g_autoptr(XbBuilderSource) source = xb_builder_source_new ();

		/* load from keyfile */
#if LIBXMLB_CHECK_VERSION(0,1,15)
		xb_builder_source_add_simple_adapter (source, "text/plain,.quirk",
						      fu_quirks_convert_quirk_to_xml_cb,
						      NULL, NULL);
#else
		xb_builder_source_add_adapter (source, "text/plain,.quirk",
					       fu_quirks_convert_quirk_to_xml_cb,
					       NULL, NULL);
#endif
		if (!xb_builder_source_load_file (source, file,
						  XB_BUILDER_SOURCE_FLAG_WATCH_FILE |
						  XB_BUILDER_SOURCE_FLAG_LITERAL_TEXT,
						  NULL, error)) {
			g_prefix_error (error, "failed to load %s: ", filename);
			return FALSE;
		}

		/* watch the file for changes */
		xb_builder_import_source (builder, source);
	}

	/* success */
	return TRUE;
}

static gboolean
fu_quirks_check_silo (FuQuirks *self, GError **error)
{
	XbBuilderCompileFlags compile_flags = XB_BUILDER_COMPILE_FLAG_WATCH_BLOB;
	g_autofree gchar *cachedirpkg = NULL;
	g_autofree gchar *datadir = NULL;
	g_autofree gchar *localstatedir = NULL;
	g_autofree gchar *xmlbfn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(XbBuilder) builder = NULL;

	/* everything is okay */
	if (self->silo != NULL && xb_silo_is_valid (self->silo))
		return TRUE;

	/* system datadir */
	builder = xb_builder_new ();
	datadir = fu_common_get_path (FU_PATH_KIND_DATADIR_PKG);
	if (!fu_quirks_add_quirks_for_path (self, builder, datadir, error))
		return FALSE;

	/* something we can write when using Ostree */
	localstatedir = fu_common_get_path (FU_PATH_KIND_LOCALSTATEDIR_PKG);
	if (!fu_quirks_add_quirks_for_path (self, builder, localstatedir, error))
		return FALSE;

	/* load silo */
	cachedirpkg = fu_common_get_path (FU_PATH_KIND_CACHEDIR_PKG);
	xmlbfn = g_build_filename (cachedirpkg, "quirks.xmlb", NULL);
	file = g_file_new_for_path (xmlbfn);
	if (g_getenv ("XMLB_VERBOSE") != NULL) {
		xb_builder_set_profile_flags (builder,
					      XB_SILO_PROFILE_FLAG_XPATH |
					      XB_SILO_PROFILE_FLAG_DEBUG);
	}
	if (self->load_flags & FU_QUIRKS_LOAD_FLAG_READONLY_FS)
		compile_flags |= XB_BUILDER_COMPILE_FLAG_IGNORE_GUID;
	self->silo = xb_builder_ensure (builder, file, compile_flags, NULL, error);
	return self->silo != NULL;
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
	g_autofree gchar *group_key = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) n = NULL;
	g_autoptr(XbQuery) query = NULL;

	g_return_val_if_fail (FU_IS_QUIRKS (self), NULL);
	g_return_val_if_fail (group != NULL, NULL);
	g_return_val_if_fail (key != NULL, NULL);

	/* ensure up to date */
	if (!fu_quirks_check_silo (self, &error)) {
		g_warning ("failed to build silo: %s", error->message);
		return NULL;
	}

	/* query */
	group_key = fu_quirks_build_group_key (group);
	query = xb_query_new_full (self->silo,
				   "quirk/device[@id=?]/value[@key=?]",
				   XB_QUERY_FLAG_NONE,
				   &error);
	if (query == NULL) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return NULL;
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return NULL;
		g_warning ("failed to build query: %s", error->message);
		return NULL;
	}
	if (!xb_query_bind_str (query, 0, group_key, &error)) {
		g_warning ("failed to bind 0: %s", error->message);
		return NULL;
	}
	if (!xb_query_bind_str (query, 1, key, &error)) {
		g_warning ("failed to bind 1: %s", error->message);
		return NULL;
	}
	n = xb_silo_query_first_full (self->silo, query, &error);
	if (n == NULL) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return NULL;
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return NULL;
		g_warning ("failed to query: %s", error->message);
		return NULL;
	}
	return xb_node_get_text (n);
}

/**
 * fu_quirks_lookup_by_id_iter:
 * @self: A #FuQuirks
 * @group: string of group to lookup
 * @iter_cb: (scope async): A #FuQuirksIter
 * @user_data: user data passed to @iter_cb
 *
 * Looks up all entries in the hardware database using a GUID value.
 *
 * Returns: %TRUE if the ID was found, and @iter was called
 *
 * Since: 1.3.3
 **/
gboolean
fu_quirks_lookup_by_id_iter (FuQuirks *self, const gchar *group,
			     FuQuirksIter iter_cb, gpointer user_data)
{
	g_autofree gchar *group_key = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) results = NULL;
	g_autoptr(XbQuery) query = NULL;

	g_return_val_if_fail (FU_IS_QUIRKS (self), FALSE);
	g_return_val_if_fail (group != NULL, FALSE);
	g_return_val_if_fail (iter_cb != NULL, FALSE);

	/* ensure up to date */
	if (!fu_quirks_check_silo (self, &error)) {
		g_warning ("failed to build silo: %s", error->message);
		return FALSE;
	}

	/* query */
	group_key = fu_quirks_build_group_key (group);
	query = xb_query_new_full (self->silo,
				   "quirk/device[@id=?]/value",
				   XB_QUERY_FLAG_NONE,
				   &error);
	if (query == NULL) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return FALSE;
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return FALSE;
		g_warning ("failed to build query: %s", error->message);
		return FALSE;
	}
	if (!xb_query_bind_str (query, 0, group_key, &error)) {
		g_warning ("failed to bind 0: %s", error->message);
		return FALSE;
	}
	results = xb_silo_query_full (self->silo, query, &error);
	if (results == NULL) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return FALSE;
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return FALSE;
		g_warning ("failed to query: %s", error->message);
		return FALSE;
	}
	for (guint i = 0; i < results->len; i++) {
		XbNode *n = g_ptr_array_index (results, i);
		iter_cb (self,
			 xb_node_get_attr (n, "key"),
			 xb_node_get_text (n),
			 user_data);
	}
	return TRUE;
}

/**
 * fu_quirks_load: (skip)
 * @self: A #FuQuirks
 * @load_flags: A #FuQuirksLoadFlags
 * @error: A #GError, or %NULL
 *
 * Loads the various files that define the hardware quirks used in plugins.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.0.1
 **/
gboolean
fu_quirks_load (FuQuirks *self, FuQuirksLoadFlags load_flags, GError **error)
{
	g_return_val_if_fail (FU_IS_QUIRKS (self), FALSE);
	self->load_flags = load_flags;
	return fu_quirks_check_silo (self, error);
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
}

static void
fu_quirks_finalize (GObject *obj)
{
	FuQuirks *self = FU_QUIRKS (obj);
	if (self->silo != NULL)
		g_object_unref (self->silo);
	G_OBJECT_CLASS (fu_quirks_parent_class)->finalize (obj);
}

/**
 * fu_quirks_new: (skip)
 *
 * Creates a new quirks object.
 *
 * Return value: a new #FuQuirks
 *
 * Since: 1.0.1
 **/
FuQuirks *
fu_quirks_new (void)
{
	FuQuirks *self;
	self = g_object_new (FU_TYPE_QUIRKS, NULL);
	return FU_QUIRKS (self);
}

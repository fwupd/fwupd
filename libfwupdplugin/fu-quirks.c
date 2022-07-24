/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuQuirks"

#include "config.h"

#include <gio/gio.h>
#include <string.h>

#include "fwupd-common.h"
#include "fwupd-error.h"
#include "fwupd-remote-private.h"

#include "fu-bytes.h"
#include "fu-common.h"
#include "fu-mutex.h"
#include "fu-path.h"
#include "fu-quirks.h"
#include "fu-string.h"

#ifdef _WIN32
#include <fwupd-windows.h>
#include <windows.h>
#endif

/**
 * FuQuirks:
 *
 * Quirks can be used to modify device behavior.
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
 * See also: [class@FuDevice], [class@FuPlugin]
 */

static void
fu_quirks_finalize(GObject *obj);

struct _FuQuirks {
	GObject parent_instance;
	FuQuirksLoadFlags load_flags;
	GHashTable *possible_keys;
	GPtrArray *invalid_keys;
	XbSilo *silo;
	XbQuery *query_kv;
	XbQuery *query_vs;
	gboolean verbose;
};

G_DEFINE_TYPE(FuQuirks, fu_quirks, G_TYPE_OBJECT)

static gchar *
fu_quirks_build_group_key(const gchar *group)
{
	const gchar *guid_prefixes[] = {"DeviceInstanceId=", "Guid=", "HwId=", NULL};

	/* this is a GUID */
	for (guint i = 0; guid_prefixes[i] != NULL; i++) {
		if (g_str_has_prefix(group, guid_prefixes[i])) {
			gsize len = strlen(guid_prefixes[i]);
			g_warning("using %s for %s in quirk files is deprecated!",
				  guid_prefixes[i],
				  group);
			if (fwupd_guid_is_valid(group + len))
				return g_strdup(group + len);
			return fwupd_guid_hash_string(group + len);
		}
	}

	/* fallback */
	if (fwupd_guid_is_valid(group))
		return g_strdup(group);
	return fwupd_guid_hash_string(group);
}

static gboolean
fu_quirks_validate_flags(const gchar *value, GError **error)
{
	if (value == NULL)
		return FALSE;
	for (gsize i = 0; value[i] != '\0'; i++) {
		gchar tmp = value[i];

		/* allowed special chars */
		if (tmp == ',' || tmp == '~' || tmp == '-')
			continue;
		if (!g_ascii_isalnum(tmp)) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "%c is not alphanumeric",
				    tmp);
			return FALSE;
		}
		if (g_ascii_isalpha(tmp) && !g_ascii_islower(tmp)) {
			g_set_error(error,
				    G_IO_ERROR,
				    G_IO_ERROR_INVALID_DATA,
				    "%c is not lowercase",
				    tmp);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static GBytes *
fu_quirks_convert_keyfile_to_xml(FuQuirks *self, GBytes *bytes, GError **error)
{
	gsize xmlsz;
	g_autofree gchar *xml = NULL;
	g_auto(GStrv) groups = NULL;
	g_autoptr(GKeyFile) kf = g_key_file_new();
	g_autoptr(XbBuilderNode) root = xb_builder_node_new("quirk");

	/* parse keyfile */
	if (!g_key_file_load_from_data(kf,
				       g_bytes_get_data(bytes, NULL),
				       g_bytes_get_size(bytes),
				       G_KEY_FILE_NONE,
				       error))
		return NULL;

	/* add each set of groups and keys */
	groups = g_key_file_get_groups(kf, NULL);
	for (guint i = 0; groups[i] != NULL; i++) {
		g_auto(GStrv) keys = NULL;
		g_autofree gchar *group_id = NULL;
		g_autoptr(GError) error_local = NULL;
		g_autoptr(XbBuilderNode) bn = NULL;

		/* sanity check group */
		if (g_str_has_prefix(groups[i], "HwID") ||
		    g_str_has_prefix(groups[i], "DeviceInstanceID") ||
		    g_str_has_prefix(groups[i], "GUID")) {
			g_warning("invalid group name '%s'", groups[i]);
			continue;
		}

		/* get all KVs for the entry */
		keys = g_key_file_get_keys(kf, groups[i], NULL, error);
		if (keys == NULL)
			return NULL;
		group_id = fu_quirks_build_group_key(groups[i]);
		bn = xb_builder_node_insert(root, "device", "id", group_id, NULL);
		for (guint j = 0; keys[j] != NULL; j++) {
			g_autofree gchar *value = NULL;

			/* sanity check key */
			if ((self->load_flags & FU_QUIRKS_LOAD_FLAG_NO_VERIFY) == 0 &&
			    g_hash_table_lookup(self->possible_keys, keys[j]) == NULL) {
				if (!g_ptr_array_find_with_equal_func(self->invalid_keys,
								      keys[j],
								      g_str_equal,
								      NULL)) {
					g_ptr_array_add(self->invalid_keys, g_strdup(keys[j]));
				}
			}
			value = g_key_file_get_value(kf, groups[i], keys[j], error);
			if (value == NULL)
				return NULL;

			/* sanity check flags */
			if (g_strcmp0(keys[j], FU_QUIRKS_FLAGS) == 0) {
				if (!fu_quirks_validate_flags(value, &error_local)) {
					g_warning("[%s] %s = %s is invalid: %s",
						  groups[i],
						  keys[j],
						  value,
						  error_local->message);
				}
			}

			xb_builder_node_insert_text(bn, "value", value, "key", keys[j], NULL);
		}
	}

	/* export as XML blob */
	xml = xb_builder_node_export(root, XB_NODE_EXPORT_FLAG_ADD_HEADER, error);
	if (xml == NULL)
		return NULL;
	xmlsz = strlen(xml);
	return g_bytes_new_take(g_steal_pointer(&xml), xmlsz);
}

static GInputStream *
fu_quirks_convert_quirk_to_xml_cb(XbBuilderSource *source,
				  XbBuilderSourceCtx *ctx,
				  gpointer user_data,
				  GCancellable *cancellable,
				  GError **error)
{
	FuQuirks *self = FU_QUIRKS(user_data);
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GBytes) bytes_xml = NULL;

	bytes = xb_builder_source_ctx_get_bytes(ctx, cancellable, error);
	if (bytes == NULL)
		return NULL;
	bytes_xml = fu_quirks_convert_keyfile_to_xml(self, bytes, error);
	if (bytes_xml == NULL)
		return NULL;
	return g_memory_input_stream_new_from_bytes(bytes_xml);
}

static gint
fu_quirks_filename_sort_cb(gconstpointer a, gconstpointer b)
{
	const gchar *stra = *((const gchar **)a);
	const gchar *strb = *((const gchar **)b);
	return g_strcmp0(stra, strb);
}

static gboolean
fu_quirks_add_quirks_for_path(FuQuirks *self, XbBuilder *builder, const gchar *path, GError **error)
{
	const gchar *tmp;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GPtrArray) filenames = g_ptr_array_new_with_free_func(g_free);

	if (g_getenv("FWUPD_VERBOSE") != NULL)
		g_debug("loading quirks from %s", path);

	/* add valid files to the array */
	if (!g_file_test(path, G_FILE_TEST_EXISTS))
		return TRUE;
	dir = g_dir_open(path, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((tmp = g_dir_read_name(dir)) != NULL) {
		if (!g_str_has_suffix(tmp, ".quirk")) {
			g_debug("skipping invalid file %s", tmp);
			continue;
		}
		g_ptr_array_add(filenames, g_build_filename(path, tmp, NULL));
	}

	/* sort */
	g_ptr_array_sort(filenames, fu_quirks_filename_sort_cb);

	/* process files */
	for (guint i = 0; i < filenames->len; i++) {
		const gchar *filename = g_ptr_array_index(filenames, i);
		g_autoptr(GFile) file = g_file_new_for_path(filename);
		g_autoptr(XbBuilderSource) source = xb_builder_source_new();

		/* load from keyfile */
#if LIBXMLB_CHECK_VERSION(0, 1, 15)
		xb_builder_source_add_simple_adapter(source,
						     "text/plain,.quirk",
						     fu_quirks_convert_quirk_to_xml_cb,
						     self,
						     NULL);
#else
		xb_builder_source_add_adapter(source,
					      "text/plain,.quirk",
					      fu_quirks_convert_quirk_to_xml_cb,
					      self,
					      NULL);
#endif
		if (!xb_builder_source_load_file(source,
						 file,
						 XB_BUILDER_SOURCE_FLAG_WATCH_FILE |
						     XB_BUILDER_SOURCE_FLAG_LITERAL_TEXT,
						 NULL,
						 error)) {
			g_prefix_error(error, "failed to load %s: ", filename);
			return FALSE;
		}

		/* watch the file for changes */
		xb_builder_import_source(builder, source);
	}

	/* success */
	return TRUE;
}

static gint
fu_quirks_strcasecmp_cb(gconstpointer a, gconstpointer b)
{
	const gchar *entry1 = *((const gchar **)a);
	const gchar *entry2 = *((const gchar **)b);
	return g_ascii_strcasecmp(entry1, entry2);
}

static gboolean
fu_quirks_add_quirks_for_resource(FuQuirks *self,
				  XbBuilder *builder,
				  const gchar *path,
				  GError **error)
{
	g_autoptr(GBytes) blob = NULL;
	g_autoptr(GBytes) blob_xml = NULL;
	g_autoptr(XbBuilderSource) source = xb_builder_source_new();

	/* load from keyfile */
	blob = g_resources_lookup_data(path, G_RESOURCE_LOOKUP_FLAGS_NONE, error);
	if (blob == NULL)
		return FALSE;
	blob_xml = fu_quirks_convert_keyfile_to_xml(self, blob, error);
	if (blob_xml == NULL)
		return FALSE;
	if (!xb_builder_source_load_bytes(source, blob_xml, XB_BUILDER_SOURCE_FLAG_NONE, error)) {
		g_prefix_error(error, "failed to load %s: ", path);
		return FALSE;
	}
	xb_builder_import_source(builder, source);
	return TRUE;
}

static gboolean
fu_quirks_add_quirks_for_resources(FuQuirks *self,
				   XbBuilder *builder,
				   const gchar *path,
				   GError **error)
{
	g_autofree gchar *hash_self = NULL;
	g_auto(GStrv) children = NULL;
	g_autoptr(GBytes) blob_self = NULL;
	g_autoptr(GError) error_local = NULL;
#ifdef _WIN32
	gchar self_exe[MAX_PATH];
	GetModuleFileNameA(NULL, self_exe, MAX_PATH);
#else
	const gchar *self_exe = "/proc/self/exe";
#endif

	children = g_resources_enumerate_children(path, G_RESOURCE_LOOKUP_FLAGS_NONE, &error_local);
	if (children == NULL) {
		if (g_error_matches(error_local, G_RESOURCE_ERROR, G_RESOURCE_ERROR_NOT_FOUND)) {
			g_debug("ignoring: %s", error_local->message);
			return TRUE;
		}
		g_propagate_error(error, g_steal_pointer(&error_local));
		return FALSE;
	}
	for (guint i = 0; children[i] != NULL; i++) {
		g_autofree gchar *fn = g_build_filename(path, children[i], NULL);
		if (!g_str_has_suffix(children[i], ".quirk"))
			continue;
		if (!fu_quirks_add_quirks_for_resource(self, builder, fn, error))
			return FALSE;
	}

	/* add the hash of the current binary to verify it has not changed */
	blob_self = fu_bytes_get_contents(self_exe, error);
	if (blob_self == NULL)
		return FALSE;
	hash_self = g_compute_checksum_for_bytes(G_CHECKSUM_SHA256, blob_self);
	xb_builder_append_guid(builder, hash_self);

	/* success */
	return TRUE;
}

static gboolean
fu_quirks_check_silo(FuQuirks *self, GError **error)
{
	XbBuilderCompileFlags compile_flags = XB_BUILDER_COMPILE_FLAG_WATCH_BLOB;
	g_autofree gchar *datadir = NULL;
	g_autofree gchar *localstatedir = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(XbBuilder) builder = NULL;
	g_autoptr(XbNode) n_any = NULL;

	/* everything is okay */
	if (self->silo != NULL && xb_silo_is_valid(self->silo))
		return TRUE;

	/* built-in quirks */
	builder = xb_builder_new();
	if (!fu_quirks_add_quirks_for_resources(self, builder, "/org/freedesktop/fwupd", error))
		return FALSE;

	/* system datadir */
	datadir = fu_path_from_kind(FU_PATH_KIND_DATADIR_QUIRKS);
	if (!fu_quirks_add_quirks_for_path(self, builder, datadir, error))
		return FALSE;

	/* something we can write when using Ostree */
	localstatedir = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_QUIRKS);
	if (!fu_quirks_add_quirks_for_path(self, builder, localstatedir, error))
		return FALSE;

	/* load silo */
	if (self->load_flags & FU_QUIRKS_LOAD_FLAG_NO_CACHE) {
		g_autoptr(GFileIOStream) iostr = NULL;
		file = g_file_new_tmp(NULL, &iostr, error);
		if (file == NULL)
			return FALSE;
	} else {
		g_autofree gchar *cachedirpkg = fu_path_from_kind(FU_PATH_KIND_CACHEDIR_PKG);
		g_autofree gchar *xmlbfn = g_build_filename(cachedirpkg, "quirks.xmlb", NULL);
		file = g_file_new_for_path(xmlbfn);
	}
	if (g_getenv("FWUPD_XMLB_VERBOSE") != NULL) {
		xb_builder_set_profile_flags(builder,
					     XB_SILO_PROFILE_FLAG_XPATH |
						 XB_SILO_PROFILE_FLAG_DEBUG);
	}
	if (self->load_flags & FU_QUIRKS_LOAD_FLAG_READONLY_FS)
		compile_flags |= XB_BUILDER_COMPILE_FLAG_IGNORE_GUID;
	self->silo = xb_builder_ensure(builder, file, compile_flags, NULL, error);
	if (self->silo == NULL)
		return FALSE;

	/* dump warnings to console, just once */
	if (self->invalid_keys->len > 0) {
		g_autofree gchar *str = NULL;
		g_ptr_array_sort(self->invalid_keys, fu_quirks_strcasecmp_cb);
		str = fu_strjoin(",", self->invalid_keys);
		g_debug("invalid key names: %s", str);
	}

	/* check if there is any quirk data to load, as older libxmlb versions will not be able to
	 * create the prepared query with an unknown text ID */
	n_any = xb_silo_query_first(self->silo, "quirk", NULL);
	if (n_any == NULL) {
		g_debug("no quirk data, not creating prepared queries");
		return TRUE;
	}

	/* create prepared queries to save time later */
	self->query_kv = xb_query_new_full(self->silo,
					   "quirk/device[@id=?]/value[@key=?]",
					   XB_QUERY_FLAG_OPTIMIZE,
					   error);
	if (self->query_kv == NULL) {
		g_prefix_error(error, "failed to prepare query: ");
		return FALSE;
	}
	self->query_vs = xb_query_new_full(self->silo,
					   "quirk/device[@id=?]/value",
					   XB_QUERY_FLAG_OPTIMIZE,
					   error);
	if (self->query_vs == NULL) {
		g_prefix_error(error, "failed to prepare query: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

/**
 * fu_quirks_lookup_by_id:
 * @self: a #FuQuirks
 * @guid: GUID to lookup
 * @key: an ID to match the entry, e.g. `Name`
 *
 * Looks up an entry in the hardware database using a string value.
 *
 * Returns: (transfer none): values from the database, or %NULL if not found
 *
 * Since: 1.0.1
 **/
const gchar *
fu_quirks_lookup_by_id(FuQuirks *self, const gchar *guid, const gchar *key)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(XbNode) n = NULL;
#if LIBXMLB_CHECK_VERSION(0, 3, 0)
	g_auto(XbQueryContext) context = XB_QUERY_CONTEXT_INIT();
#endif

	g_return_val_if_fail(FU_IS_QUIRKS(self), NULL);
	g_return_val_if_fail(guid != NULL, NULL);
	g_return_val_if_fail(key != NULL, NULL);

	/* ensure up to date */
	if (!fu_quirks_check_silo(self, &error)) {
		g_warning("failed to build silo: %s", error->message);
		return NULL;
	}

	/* no quirk data */
	if (self->query_kv == NULL)
		return NULL;

	/* query */
#if LIBXMLB_CHECK_VERSION(0, 3, 0)
	xb_query_context_set_flags(&context, XB_QUERY_FLAG_USE_INDEXES);
	xb_value_bindings_bind_str(xb_query_context_get_bindings(&context), 0, guid, NULL);
	xb_value_bindings_bind_str(xb_query_context_get_bindings(&context), 1, key, NULL);
	n = xb_silo_query_first_with_context(self->silo, self->query_kv, &context, &error);
#else
	if (!xb_query_bind_str(self->query_kv, 0, guid, &error)) {
		g_warning("failed to bind 0: %s", error->message);
		return NULL;
	}
	if (!xb_query_bind_str(self->query_kv, 1, key, &error)) {
		g_warning("failed to bind 1: %s", error->message);
		return NULL;
	}
	n = xb_silo_query_first_full(self->silo, self->query_kv, &error);
#endif

	if (n == NULL) {
		if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return NULL;
		if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return NULL;
		g_warning("failed to query: %s", error->message);
		return NULL;
	}
	if (self->verbose)
		g_debug("%s:%s → %s", guid, key, xb_node_get_text(n));
	return xb_node_get_text(n);
}

/**
 * fu_quirks_lookup_by_id_iter:
 * @self: a #FuQuirks
 * @guid: GUID to lookup
 * @iter_cb: (scope async): a function to call for each result
 * @user_data: user data passed to @iter_cb
 *
 * Looks up all entries in the hardware database using a GUID value.
 *
 * Returns: %TRUE if the ID was found, and @iter was called
 *
 * Since: 1.3.3
 **/
gboolean
fu_quirks_lookup_by_id_iter(FuQuirks *self,
			    const gchar *guid,
			    FuQuirksIter iter_cb,
			    gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) results = NULL;
#if LIBXMLB_CHECK_VERSION(0, 3, 0)
	g_auto(XbQueryContext) context = XB_QUERY_CONTEXT_INIT();
#endif

	g_return_val_if_fail(FU_IS_QUIRKS(self), FALSE);
	g_return_val_if_fail(guid != NULL, FALSE);
	g_return_val_if_fail(iter_cb != NULL, FALSE);

	/* ensure up to date */
	if (!fu_quirks_check_silo(self, &error)) {
		g_warning("failed to build silo: %s", error->message);
		return FALSE;
	}

	/* no quirk data */
	if (self->query_vs == NULL)
		return FALSE;

	/* query */
#if LIBXMLB_CHECK_VERSION(0, 3, 0)
	xb_query_context_set_flags(&context, XB_QUERY_FLAG_USE_INDEXES);
	xb_value_bindings_bind_str(xb_query_context_get_bindings(&context), 0, guid, NULL);
	results = xb_silo_query_with_context(self->silo, self->query_vs, &context, &error);
#else
	if (!xb_query_bind_str(self->query_vs, 0, guid, &error)) {
		g_warning("failed to bind 0: %s", error->message);
		return FALSE;
	}
	results = xb_silo_query_full(self->silo, self->query_vs, &error);
#endif

	if (results == NULL) {
		if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return FALSE;
		if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return FALSE;
		g_warning("failed to query: %s", error->message);
		return FALSE;
	}
	for (guint i = 0; i < results->len; i++) {
		XbNode *n = g_ptr_array_index(results, i);
		if (self->verbose)
			g_debug("%s → %s", guid, xb_node_get_text(n));
		iter_cb(self, xb_node_get_attr(n, "key"), xb_node_get_text(n), user_data);
	}
	return TRUE;
}

/**
 * fu_quirks_load: (skip)
 * @self: a #FuQuirks
 * @load_flags: load flags
 * @error: (nullable): optional return location for an error
 *
 * Loads the various files that define the hardware quirks used in plugins.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.0.1
 **/
gboolean
fu_quirks_load(FuQuirks *self, FuQuirksLoadFlags load_flags, GError **error)
{
	g_return_val_if_fail(FU_IS_QUIRKS(self), FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);
	self->load_flags = load_flags;
	self->verbose = g_getenv("FWUPD_XMLB_VERBOSE") != NULL;
	return fu_quirks_check_silo(self, error);
}

/**
 * fu_quirks_add_possible_key:
 * @self: a #FuQuirks
 * @possible_key: a key name, e.g. `Flags`
 *
 * Adds a possible quirk key. If added by a plugin it should be namespaced
 * using the plugin name, where possible.
 *
 * Since: 1.5.8
 **/
void
fu_quirks_add_possible_key(FuQuirks *self, const gchar *possible_key)
{
	g_return_if_fail(FU_IS_QUIRKS(self));
	g_return_if_fail(possible_key != NULL);
	g_hash_table_add(self->possible_keys, g_strdup(possible_key));
}

static void
fu_quirks_class_init(FuQuirksClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_quirks_finalize;
}

static void
fu_quirks_init(FuQuirks *self)
{
	self->possible_keys = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	self->invalid_keys = g_ptr_array_new_with_free_func(g_free);

	/* built in */
	fu_quirks_add_possible_key(self, FU_QUIRKS_BRANCH);
	fu_quirks_add_possible_key(self, FU_QUIRKS_CHILDREN);
	fu_quirks_add_possible_key(self, FU_QUIRKS_COUNTERPART_GUID);
	fu_quirks_add_possible_key(self, FU_QUIRKS_FIRMWARE_SIZE);
	fu_quirks_add_possible_key(self, FU_QUIRKS_FIRMWARE_SIZE_MAX);
	fu_quirks_add_possible_key(self, FU_QUIRKS_FIRMWARE_SIZE_MIN);
	fu_quirks_add_possible_key(self, FU_QUIRKS_FLAGS);
	fu_quirks_add_possible_key(self, FU_QUIRKS_GTYPE);
	fu_quirks_add_possible_key(self, FU_QUIRKS_FIRMWARE_GTYPE);
	fu_quirks_add_possible_key(self, FU_QUIRKS_GUID);
	fu_quirks_add_possible_key(self, FU_QUIRKS_ICON);
	fu_quirks_add_possible_key(self, FU_QUIRKS_INHIBIT);
	fu_quirks_add_possible_key(self, FU_QUIRKS_INSTALL_DURATION);
	fu_quirks_add_possible_key(self, FU_QUIRKS_ISSUE);
	fu_quirks_add_possible_key(self, FU_QUIRKS_NAME);
	fu_quirks_add_possible_key(self, FU_QUIRKS_PARENT_GUID);
	fu_quirks_add_possible_key(self, FU_QUIRKS_PLUGIN);
	fu_quirks_add_possible_key(self, FU_QUIRKS_PRIORITY);
	fu_quirks_add_possible_key(self, FU_QUIRKS_PROTOCOL);
	fu_quirks_add_possible_key(self, FU_QUIRKS_PROXY_GUID);
	fu_quirks_add_possible_key(self, FU_QUIRKS_BATTERY_THRESHOLD);
	fu_quirks_add_possible_key(self, FU_QUIRKS_REMOVE_DELAY);
	fu_quirks_add_possible_key(self, FU_QUIRKS_SUMMARY);
	fu_quirks_add_possible_key(self, FU_QUIRKS_UPDATE_IMAGE);
	fu_quirks_add_possible_key(self, FU_QUIRKS_UPDATE_MESSAGE);
	fu_quirks_add_possible_key(self, FU_QUIRKS_VENDOR);
	fu_quirks_add_possible_key(self, FU_QUIRKS_VENDOR_ID);
	fu_quirks_add_possible_key(self, FU_QUIRKS_VERSION);
	fu_quirks_add_possible_key(self, FU_QUIRKS_VERSION_FORMAT);
	fu_quirks_add_possible_key(self, FU_QUIRKS_CFI_DEVICE_CMD_READ_ID);
	fu_quirks_add_possible_key(self, FU_QUIRKS_CFI_DEVICE_CMD_READ_ID_SZ);
	fu_quirks_add_possible_key(self, FU_QUIRKS_CFI_DEVICE_CMD_CHIP_ERASE);
	fu_quirks_add_possible_key(self, FU_QUIRKS_CFI_DEVICE_CMD_BLOCK_ERASE);
	fu_quirks_add_possible_key(self, FU_QUIRKS_CFI_DEVICE_CMD_SECTOR_ERASE);
	fu_quirks_add_possible_key(self, FU_QUIRKS_CFI_DEVICE_CMD_WRITE_STATUS);
	fu_quirks_add_possible_key(self, FU_QUIRKS_CFI_DEVICE_CMD_PAGE_PROG);
	fu_quirks_add_possible_key(self, FU_QUIRKS_CFI_DEVICE_CMD_READ_DATA);
	fu_quirks_add_possible_key(self, FU_QUIRKS_CFI_DEVICE_CMD_READ_STATUS);
	fu_quirks_add_possible_key(self, FU_QUIRKS_CFI_DEVICE_CMD_WRITE_EN);
	fu_quirks_add_possible_key(self, FU_QUIRKS_CFI_DEVICE_PAGE_SIZE);
	fu_quirks_add_possible_key(self, FU_QUIRKS_CFI_DEVICE_SECTOR_SIZE);
	fu_quirks_add_possible_key(self, FU_QUIRKS_CFI_DEVICE_BLOCK_SIZE);
}

static void
fu_quirks_finalize(GObject *obj)
{
	FuQuirks *self = FU_QUIRKS(obj);
	if (self->query_kv != NULL)
		g_object_unref(self->query_kv);
	if (self->query_vs != NULL)
		g_object_unref(self->query_vs);
	if (self->silo != NULL)
		g_object_unref(self->silo);
	g_hash_table_unref(self->possible_keys);
	g_ptr_array_unref(self->invalid_keys);
	G_OBJECT_CLASS(fu_quirks_parent_class)->finalize(obj);
}

/**
 * fu_quirks_new: (skip)
 *
 * Creates a new quirks object.
 *
 * Returns: a new #FuQuirks
 *
 * Since: 1.0.1
 **/
FuQuirks *
fu_quirks_new(void)
{
	FuQuirks *self;
	self = g_object_new(FU_TYPE_QUIRKS, NULL);
	return FU_QUIRKS(self);
}

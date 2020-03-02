/*
 * Copyright (C) 2017-2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuRemoteList"

#include "config.h"

#include <xmlb.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <glib/gi18n.h>

#include "fu-common.h"
#include "fu-remote-list.h"

#include "fwupd-common.h"
#include "fwupd-error.h"
#include "fwupd-remote-private.h"

enum {
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

static void fu_remote_list_finalize	 (GObject *obj);

struct _FuRemoteList
{
	GObject			 parent_instance;
	GPtrArray		*array;			/* (element-type FwupdRemote) */
	GPtrArray		*monitors;		/* (element-type GFileMonitor) */
	XbSilo			*silo;
};

G_DEFINE_TYPE (FuRemoteList, fu_remote_list, G_TYPE_OBJECT)

static void
fu_remote_list_emit_changed (FuRemoteList *self)
{
	g_debug ("::remote_list changed");
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static void
fu_remote_list_monitor_changed_cb (GFileMonitor *monitor,
				   GFile *file,
				   GFile *other_file,
				   GFileMonitorEvent event_type,
				   gpointer user_data)
{
	FuRemoteList *self = FU_REMOTE_LIST (user_data);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *filename = g_file_get_path (file);
	g_debug ("%s changed, reloading all remotes", filename);
	if (!fu_remote_list_reload (self, &error))
		g_warning ("failed to rescan remotes: %s", error->message);
	fu_remote_list_emit_changed (self);
}

static guint64
_fwupd_remote_get_mtime (FwupdRemote *remote)
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
fu_remote_list_add_inotify (FuRemoteList *self, const gchar *filename, GError **error)
{
	GFileMonitor *monitor;
	g_autoptr(GFile) file = g_file_new_for_path (filename);

	/* set up a notify watch */
	monitor = g_file_monitor (file, G_FILE_MONITOR_NONE, NULL, error);
	if (monitor == NULL)
		return FALSE;
	g_signal_connect (monitor, "changed",
			  G_CALLBACK (fu_remote_list_monitor_changed_cb), self);
	g_ptr_array_add (self->monitors, monitor);
	return TRUE;
}

static GString *
_fwupd_remote_get_agreement_default (FwupdRemote *self, GError **error)
{
	GString *str = g_string_new (NULL);

	/* this is designed as a fallback; the actual warning should ideally
	 * come from the LVFS instance that is serving the remote */
	g_string_append_printf (str, "<p>%s</p>",
				/* TRANSLATORS: show the user a warning */
				_("Your distributor may not have verified any of "
				  "the firmware updates for compatibility with your "
				  "system or connected devices."));
	g_string_append_printf (str, "<p>%s</p>",
				/* TRANSLATORS: show the user a warning */
				_("Enabling this remote is done at your own risk."));
	return str;
}

static GString *
_fwupd_remote_get_agreement_for_app (FwupdRemote *self, XbNode *component, GError **error)
{
	g_autofree gchar *tmp = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(XbNode) n = NULL;

	/* manually find the first agreement section */
	n = xb_node_query_first (component, "agreement/agreement_section/description/*", &error_local);
	if (n == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "No agreement description found: %s",
			     error_local->message);
		return NULL;
	}
	tmp = xb_node_export (n, XB_NODE_EXPORT_FLAG_INCLUDE_SIBLINGS, error);
	if (tmp == NULL)
		return NULL;
	return g_string_new (tmp);
}

static gchar *
_fwupd_remote_build_component_id (FwupdRemote *remote)
{
	return g_strdup_printf ("org.freedesktop.fwupd.remotes.%s",
				fwupd_remote_get_id (remote));
}

static gboolean
fu_remote_list_add_for_path (FuRemoteList *self, const gchar *path, GError **error)
{
	const gchar *tmp;
	g_autofree gchar *path_remotes = NULL;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GHashTable) os_release = NULL;

	path_remotes = g_build_filename (path, "remotes.d", NULL);
	if (!g_file_test (path_remotes, G_FILE_TEST_EXISTS)) {
		g_debug ("path %s does not exist", path_remotes);
		return TRUE;
	}
	if (!fu_remote_list_add_inotify (self, path_remotes, error))
		return FALSE;
	dir = g_dir_open (path_remotes, 0, error);
	if (dir == NULL)
		return FALSE;
	os_release = fwupd_get_os_release (error);
	if (os_release == NULL)
		return FALSE;
	while ((tmp = g_dir_read_name (dir)) != NULL) {
		g_autofree gchar *filename = g_build_filename (path_remotes, tmp, NULL);
		g_autoptr(FwupdRemote) remote = fwupd_remote_new ();
		g_autofree gchar *localstatedir = NULL;
		g_autofree gchar *remotesdir = NULL;

		/* skip invalid files */
		if (!g_str_has_suffix (tmp, ".conf")) {
			g_debug ("skipping invalid file %s", filename);
			continue;
		}

		/* set directory to store data */
		localstatedir = fu_common_get_path (FU_PATH_KIND_LOCALSTATEDIR_PKG);
		remotesdir = g_build_filename (localstatedir, "remotes.d", NULL);
		fwupd_remote_set_remotes_dir (remote, remotesdir);

		/* load from keyfile */
		g_debug ("loading remotes from %s", filename);
		if (!fwupd_remote_load_from_filename (remote, filename,
						      NULL, error)) {
			g_prefix_error (error, "failed to load %s: ", filename);
			return FALSE;
		}

		/* watch the remote_list file and the XML file itself */
		if (!fu_remote_list_add_inotify (self, filename, error))
			return FALSE;
		if (!fu_remote_list_add_inotify (self, fwupd_remote_get_filename_cache (remote), error))
			return FALSE;

		/* try to find a custom agreement, falling back to a generic warning */
		if (fwupd_remote_get_kind (remote) == FWUPD_REMOTE_KIND_DOWNLOAD) {
			g_autoptr(GString) agreement_markup = NULL;
			g_autofree gchar *component_id = _fwupd_remote_build_component_id (remote);
			g_autoptr(XbNode) component = NULL;
			g_autofree gchar *xpath = NULL;

			xpath = g_strdup_printf ("component/id[text()='%s']/..", component_id);
			component = xb_silo_query_first (self->silo, xpath, NULL);
			if (component != NULL) {
				agreement_markup = _fwupd_remote_get_agreement_for_app (remote, component, error);
			} else {
				agreement_markup = _fwupd_remote_get_agreement_default (remote, error);
			}
			if (agreement_markup == NULL)
				return FALSE;

			/* replace any dynamic values from os-release */
			tmp = g_hash_table_lookup (os_release, "NAME");
			if (tmp == NULL)
				tmp = "this distribution";
			fu_common_string_replace (agreement_markup, "$OS_RELEASE:NAME$", tmp);
			tmp = g_hash_table_lookup (os_release, "BUG_REPORT_URL");
			if (tmp == NULL)
				tmp = "https://github.com/fwupd/fwupd/issues";
			fu_common_string_replace (agreement_markup, "$OS_RELEASE:BUG_REPORT_URL$", tmp);
			fwupd_remote_set_agreement (remote, agreement_markup->str);
		}

		/* set mtime */
		fwupd_remote_set_mtime (remote, _fwupd_remote_get_mtime (remote));
		g_ptr_array_add (self->array, g_steal_pointer (&remote));
	}
	return TRUE;
}

gboolean
fu_remote_list_set_key_value (FuRemoteList *self,
			      const gchar *remote_id,
			      const gchar *key,
			      const gchar *value,
			      GError **error)
{
	FwupdRemote *remote;
	const gchar *filename;
	g_autoptr(GKeyFile) keyfile = g_key_file_new ();

	/* check remote is valid */
	remote = fu_remote_list_get_by_id (self, remote_id);
	if (remote == NULL) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_FOUND,
			     "remote %s not found", remote_id);
		return FALSE;
	}

	/* modify the remote */
	filename = fwupd_remote_get_filename_source (remote);
	if (!g_key_file_load_from_file (keyfile, filename,
					G_KEY_FILE_KEEP_COMMENTS,
					error)) {
		g_prefix_error (error, "failed to load %s: ", filename);
		return FALSE;
	}
	g_key_file_set_string (keyfile, "fwupd Remote", key, value);
	if (!g_key_file_save_to_file (keyfile, filename, error))
		return FALSE;

	/* reload values */
	if (!fwupd_remote_load_from_filename (remote, filename,
					      NULL, error)) {
		g_prefix_error (error, "failed to load %s: ", filename);
		return FALSE;
	}
	fu_remote_list_emit_changed (self);
	return TRUE;
}

static gint
fu_remote_list_sort_cb (gconstpointer a, gconstpointer b)
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

static guint
fu_remote_list_depsolve_with_direction (FuRemoteList *self, gint inc)
{
	guint cnt = 0;
	for (guint i = 0; i < self->array->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (self->array, i);
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
			remote2 = fu_remote_list_get_by_id (self, order[j]);
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

gboolean
fu_remote_list_reload (FuRemoteList *self, GError **error)
{
	guint depsolve_check;
	g_autofree gchar *remotesdir = NULL;

	/* clear */
	g_ptr_array_set_size (self->array, 0);
	g_ptr_array_set_size (self->monitors, 0);

	/* use sysremotes, and then fall back to /etc */
	remotesdir = fu_common_get_path (FU_PATH_KIND_SYSCONFDIR_PKG);
	if (!g_file_test (remotesdir, G_FILE_TEST_EXISTS)) {
		g_debug ("no remotes found");
		return TRUE;
	}

	/* look for all remote_list */
	if (!fu_remote_list_add_for_path (self, remotesdir, error))
		return FALSE;

	/* depsolve */
	for (depsolve_check = 0; depsolve_check < 100; depsolve_check++) {
		guint cnt = 0;
		cnt += fu_remote_list_depsolve_with_direction (self, 1);
		cnt += fu_remote_list_depsolve_with_direction (self, -1);
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
	g_ptr_array_sort (self->array, fu_remote_list_sort_cb);

	/* success */
	return TRUE;
}

static gboolean
fu_remote_list_load_metainfos (XbBuilder *builder, GError **error)
{
	const gchar *fn;
	g_autofree gchar *datadir = NULL;
	g_autofree gchar *metainfo_path = NULL;
	g_autoptr(GDir) dir = NULL;

	/* pkg metainfo dir */
	datadir = fu_common_get_path (FU_PATH_KIND_DATADIR_PKG);
	metainfo_path = g_build_filename (datadir, "metainfo", NULL);
	if (!g_file_test (metainfo_path, G_FILE_TEST_EXISTS))
		return TRUE;

	g_debug ("loading %s", metainfo_path);
	dir = g_dir_open (metainfo_path, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((fn = g_dir_read_name (dir)) != NULL) {
		if (g_str_has_suffix (fn, ".metainfo.xml")) {
			g_autofree gchar *filename = g_build_filename (metainfo_path, fn, NULL);
			g_autoptr(GFile) file = g_file_new_for_path (filename);
			g_autoptr(XbBuilderSource) source = xb_builder_source_new ();
			if (!xb_builder_source_load_file (source, file,
							  XB_BUILDER_SOURCE_FLAG_NONE,
							  NULL, error))
				return FALSE;
			xb_builder_import_source (builder, source);
		}
	}
	return TRUE;
}

gboolean
fu_remote_list_load (FuRemoteList *self, FuRemoteListLoadFlags flags, GError **error)
{
	const gchar *const *locales = g_get_language_names ();
	g_autofree gchar *cachedirpkg = NULL;
	g_autofree gchar *xmlbfn = NULL;
	g_autoptr(GFile) xmlb = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new ();
	XbBuilderCompileFlags compile_flags = XB_BUILDER_COMPILE_FLAG_SINGLE_LANG |
					      XB_BUILDER_COMPILE_FLAG_IGNORE_INVALID;

	g_return_val_if_fail (FU_IS_REMOTE_LIST (self), FALSE);
	g_return_val_if_fail (self->silo == NULL, FALSE);

	/* load AppStream about the remote_list */
	if (!fu_remote_list_load_metainfos (builder, error))
		return FALSE;

	/* add the locales, which is really only going to be 'C' or 'en' */
	for (guint i = 0; locales[i] != NULL; i++)
		xb_builder_add_locale (builder, locales[i]);

	/* on a read-only filesystem don't care about the cache GUID */
	if (flags & FU_REMOTE_LIST_LOAD_FLAG_READONLY_FS)
		compile_flags |= XB_BUILDER_COMPILE_FLAG_IGNORE_GUID;

	/* build the metainfo silo */
	cachedirpkg = fu_common_get_path (FU_PATH_KIND_CACHEDIR_PKG);
	xmlbfn = g_build_filename (cachedirpkg, "metainfo.xmlb", NULL);
	xmlb = g_file_new_for_path (xmlbfn);
	self->silo = xb_builder_ensure (builder, xmlb, compile_flags, NULL, error);
	if (self->silo == NULL)
		return FALSE;

	/* load remote_list */
	return fu_remote_list_reload (self, error);
}

GPtrArray *
fu_remote_list_get_all (FuRemoteList *self)
{
	g_return_val_if_fail (FU_IS_REMOTE_LIST (self), NULL);
	return self->array;
}

FwupdRemote *
fu_remote_list_get_by_id (FuRemoteList *self, const gchar *remote_id)
{
	g_return_val_if_fail (FU_IS_REMOTE_LIST (self), NULL);
	for (guint i = 0; i < self->array->len; i++) {
		FwupdRemote *remote = g_ptr_array_index (self->array, i);
		if (g_strcmp0 (remote_id, fwupd_remote_get_id (remote)) == 0)
			return remote;
	}
	return NULL;
}

static void
fu_remote_list_class_init (FuRemoteListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_remote_list_finalize;

	signals[SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

static void
fu_remote_list_init (FuRemoteList *self)
{
	self->array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	self->monitors = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

static void
fu_remote_list_finalize (GObject *obj)
{
	FuRemoteList *self = FU_REMOTE_LIST (obj);
	if (self->silo != NULL)
		g_object_unref (self->silo);
	g_ptr_array_unref (self->array);
	g_ptr_array_unref (self->monitors);
	G_OBJECT_CLASS (fu_remote_list_parent_class)->finalize (obj);
}

FuRemoteList *
fu_remote_list_new (void)
{
	FuRemoteList *self;
	self = g_object_new (FU_TYPE_REMOTE_LIST, NULL);
	return FU_REMOTE_LIST (self);
}

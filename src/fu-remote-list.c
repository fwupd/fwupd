/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuRemoteList"

#include "config.h"

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <xmlb.h>

#ifdef HAVE_INOTIFY_H
#include <errno.h>
#include <sys/inotify.h>
#endif

#include "fwupd-remote-private.h"

#include "fu-remote-list.h"
#include "fu-remote.h"

enum { SIGNAL_CHANGED, SIGNAL_ADDED, SIGNAL_LAST };

static guint signals[SIGNAL_LAST] = {0};

static gboolean
fu_remote_list_reload(FuRemoteList *self, GError **error);
static void
fu_remote_list_finalize(GObject *obj);

struct _FuRemoteList {
	GObject parent_instance;
	GPtrArray *array;    /* (element-type FwupdRemote) */
	GPtrArray *monitors; /* (element-type GFileMonitor) */
	gboolean testing_remote;
	gboolean fix_metadata_uri;
	XbSilo *silo;
	gchar *lvfs_metadata_format;
};

G_DEFINE_TYPE(FuRemoteList, fu_remote_list, G_TYPE_OBJECT)

static void
fu_remote_list_emit_changed(FuRemoteList *self)
{
	g_debug("::remote_list changed");
	g_signal_emit(self, signals[SIGNAL_CHANGED], 0);
}

static void
fu_remote_list_emit_added(FuRemoteList *self, FwupdRemote *remote)
{
	g_debug("::remote_list changed");
	g_signal_emit(self, signals[SIGNAL_ADDED], 0, remote);
}

void
fu_remote_list_set_lvfs_metadata_format(FuRemoteList *self, const gchar *lvfs_metadata_format)
{
	g_return_if_fail(FU_IS_REMOTE_LIST(self));
	g_return_if_fail(lvfs_metadata_format != NULL);
	if (g_strcmp0(lvfs_metadata_format, self->lvfs_metadata_format) == 0)
		return;
	g_free(self->lvfs_metadata_format);
	self->lvfs_metadata_format = g_strdup(lvfs_metadata_format);
}

static void
fu_remote_list_monitor_changed_cb(GFileMonitor *monitor,
				  GFile *file,
				  GFile *other_file,
				  GFileMonitorEvent event_type,
				  gpointer user_data)
{
	FuRemoteList *self = FU_REMOTE_LIST(user_data);
	g_autoptr(GError) error = NULL;
	g_autofree gchar *filename = g_file_get_path(file);
	g_info("%s changed, reloading all remotes", filename);
	if (!fu_remote_list_reload(self, &error))
		g_warning("failed to rescan remotes: %s", error->message);
	fu_remote_list_emit_changed(self);
}

static guint64
_fwupd_remote_get_mtime(FwupdRemote *remote)
{
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;

	file = g_file_new_for_path(fwupd_remote_get_filename_cache(remote));
	if (!g_file_query_exists(file, NULL))
		return G_MAXUINT64;
	info = g_file_query_info(file,
				 G_FILE_ATTRIBUTE_TIME_MODIFIED,
				 G_FILE_QUERY_INFO_NONE,
				 NULL,
				 NULL);
	if (info == NULL)
		return G_MAXUINT64;
	return g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
}

/* GLib only returns the very unhelpful "Unable to find default local file monitor type"
 * when /proc/sys/fs/inotify/max_user_instances is set too low; detect this and set a proper
 * error prefix to aid debugging when the daemon fails to start */
static void
fu_remote_list_fixup_inotify_error(GError **error)
{
#ifdef HAVE_INOTIFY_H
	int fd;
	int wd;
	const gchar *fn = "/proc/sys/fs/inotify/max_user_instances";

	fd = inotify_init();
	if (fd == -1) {
		g_prefix_error(error, "Could not initialize inotify, check %s: ", fn);
		return;
	}
	wd = inotify_add_watch(fd, "/", 0);
	if (wd < 0) {
		if (errno == ENOSPC)
			g_prefix_error(error, "No space for inotify, check %s: ", fn);
	} else {
		inotify_rm_watch(fd, wd);
	}
	close(fd);
#endif
}

static gboolean
fu_remote_list_add_inotify(FuRemoteList *self, const gchar *filename, GError **error)
{
	GFileMonitor *monitor;
	g_autoptr(GFile) file = g_file_new_for_path(filename);

	/* set up a notify watch */
	monitor = g_file_monitor(file, G_FILE_MONITOR_NONE, NULL, error);
	if (monitor == NULL) {
		fu_remote_list_fixup_inotify_error(error);
		return FALSE;
	}
	g_signal_connect(G_FILE_MONITOR(monitor),
			 "changed",
			 G_CALLBACK(fu_remote_list_monitor_changed_cb),
			 self);
	g_ptr_array_add(self->monitors, monitor);
	return TRUE;
}

static GString *
_fwupd_remote_get_agreement_default(FwupdRemote *self, GError **error)
{
	GString *str = g_string_new(NULL);

	/* this is designed as a fallback; the actual warning should ideally
	 * come from the LVFS instance that is serving the remote */
	g_string_append_printf(str,
			       "<p>%s</p>",
			       /* TRANSLATORS: show the user a warning */
			       _("Your distributor may not have verified any of "
				 "the firmware updates for compatibility with your "
				 "system or connected devices."));
	g_string_append_printf(str,
			       "<p>%s</p>",
			       /* TRANSLATORS: show the user a warning */
			       _("Enabling this remote is done at your own risk."));
	return str;
}

static GString *
_fwupd_remote_get_agreement_for_app(FwupdRemote *self, XbNode *component, GError **error)
{
	g_autofree gchar *tmp = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(XbNode) n = NULL;

	/* manually find the first agreement section */
	n = xb_node_query_first(component,
				"agreement/agreement_section/description/*",
				&error_local);
	if (n == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "No agreement description found: %s",
			    error_local->message);
		return NULL;
	}
	tmp = xb_node_export(n, XB_NODE_EXPORT_FLAG_INCLUDE_SIBLINGS, error);
	if (tmp == NULL)
		return NULL;
	return g_string_new(tmp);
}

static gchar *
_fwupd_remote_build_component_id(FwupdRemote *remote)
{
	return g_strdup_printf("org.freedesktop.fwupd.remotes.%s", fwupd_remote_get_id(remote));
}

static gchar *
fu_remote_list_get_last_ext(const gchar *filename)
{
	gchar *tmp;
	g_return_val_if_fail(filename != NULL, NULL);
	tmp = g_strrstr(filename, ".");
	if (tmp == NULL)
		return NULL;
	return g_strdup(tmp + 1);
}

static gboolean
fu_remote_list_remote_filename_cache_fn_is_obsolete(FuRemoteList *self, const gchar *fn)
{
	g_autofree gchar *ext = fu_remote_list_get_last_ext(fn);
	g_autofree gchar *basename = g_path_get_basename(fn);

	/* fwupd >= 2.0.0 calls this firmware.xml.* so that we can validate with jcat-tool */
	if (g_str_has_prefix(basename, "metadata."))
		return TRUE;

	/* in a format that we no longer use */
	if (g_strcmp0(ext, "jcat") == 0)
		return FALSE;
	return g_strcmp0(ext, self->lvfs_metadata_format) != 0;
}

static gboolean
fu_remote_list_cleanup_lvfs_remote(FuRemoteList *self, FwupdRemote *remote, GError **error)
{
	const gchar *fn_cache = fwupd_remote_get_filename_cache(remote);
	g_autofree gchar *dirname = NULL;
	g_autoptr(GPtrArray) files = NULL;

	/* sanity check */
	if (fn_cache == NULL)
		return TRUE;
	if (self->lvfs_metadata_format == NULL)
		return TRUE;

	/* get all files */
	dirname = g_path_get_dirname(fn_cache);
	files = fu_path_get_files(dirname, NULL);
	if (files == NULL)
		return TRUE;

	/* delete any obsolete ones */
	for (guint i = 0; i < files->len; i++) {
		const gchar *fn = g_ptr_array_index(files, i);
		if (fu_remote_list_remote_filename_cache_fn_is_obsolete(self, fn)) {
			g_info("deleting obsolete %s", fn);
			if (g_unlink(fn) == -1) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "failed to delete obsolete %s",
					    fn);
				return FALSE;
			}
		}
	}

	/* success */
	return TRUE;
}

void
fu_remote_list_add_remote(FuRemoteList *self, FwupdRemote *remote)
{
	g_return_if_fail(FU_IS_REMOTE_LIST(self));
	g_return_if_fail(FWUPD_IS_REMOTE(remote));
	fu_remote_list_emit_added(self, remote);
	g_ptr_array_add(self->array, g_object_ref(remote));
}

static gboolean
fu_remote_list_is_remote_origin_lvfs(FwupdRemote *remote)
{
	if (fwupd_remote_get_id(remote) != NULL &&
	    g_strstr_len(fwupd_remote_get_id(remote), -1, "lvfs") != NULL)
		return TRUE;
	if (fwupd_remote_get_metadata_uri(remote) != NULL &&
	    g_strstr_len(fwupd_remote_get_metadata_uri(remote), -1, "fwupd.org") != NULL)
		return TRUE;
	return FALSE;
}

static gboolean
fu_remote_list_add_for_file(FuRemoteList *self,
			    const gchar *filename,
			    GError **error)
{
	FwupdRemote *remote_tmp;
	g_autofree gchar *remotesdir = NULL;
	g_autoptr(FwupdRemote) remote = fu_remote_new();

	/* set directory to store data */
	remotesdir = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_METADATA);
	fwupd_remote_set_remotes_dir(remote, remotesdir);

	/* load from keyfile */
	g_info("loading remote from %s", filename);
	if (!fu_remote_load_from_filename(remote, filename, NULL, error)) {
		g_prefix_error(error, "failed to load %s: ", filename);
		return FALSE;
	}

	/* does it already exist */
	remote_tmp = fu_remote_list_get_by_id(self, fwupd_remote_get_id(remote));
	if (remote_tmp != NULL) {
		g_debug("remote %s already added from %s",
			fwupd_remote_get_id(remote),
			fwupd_remote_get_filename_source(remote_tmp));
		return TRUE;
	}

	/* auto-fix before setup */
	if (self->fix_metadata_uri && fu_remote_list_is_remote_origin_lvfs(remote)) {
		const gchar *metadata_url = fwupd_remote_get_metadata_uri(remote);
		g_autofree gchar *ext = fu_remote_list_get_last_ext(metadata_url);
		if (g_strcmp0(ext, self->lvfs_metadata_format) != 0) {
			g_autoptr(GString) str = g_string_new(metadata_url);
			g_autofree gchar *metadata_ext =
			    g_strdup_printf(".%s", self->lvfs_metadata_format);
			g_string_replace(str, ".gz", metadata_ext, 0);
			g_string_replace(str, ".xz", metadata_ext, 0);
			g_string_replace(str, ".zst", metadata_ext, 0);
			g_info("auto-fixing remote %s MetadataURI from %s to %s",
			       fwupd_remote_get_id(remote),
			       metadata_url,
			       str->str);
			fwupd_remote_set_metadata_uri(remote, str->str);
		}
	}

	/* load remote */
	if (!fwupd_remote_setup(remote, error)) {
		g_prefix_error(error, "failed to setup %s: ", filename);
		return FALSE;
	}

	/* delete the obsolete files if the remote is now set up to use a new metadata format */
	if (fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED) &&
	    fwupd_remote_get_kind(remote) == FWUPD_REMOTE_KIND_DOWNLOAD &&
	    fu_remote_list_is_remote_origin_lvfs(remote)) {
		if (!fu_remote_list_cleanup_lvfs_remote(self, remote, error))
			return FALSE;
	}

	/* watch the remote_list file and the XML file itself */
	if (!fu_remote_list_add_inotify(self, filename, error))
		return FALSE;
	if (!fu_remote_list_add_inotify(self, fwupd_remote_get_filename_cache(remote), error))
		return FALSE;

	/* try to find a custom agreement, falling back to a generic warning */
	if (fwupd_remote_get_kind(remote) == FWUPD_REMOTE_KIND_DOWNLOAD) {
		g_autofree gchar *component_id = _fwupd_remote_build_component_id(remote);
		g_autofree gchar *xpath = NULL;
		g_autoptr(GString) agreement_markup = NULL;
		g_autoptr(XbNode) component = NULL;
		struct {
			const gchar *key;
			const gchar *search;
			const gchar *fallback;
		} distro_kv[] = {
		    {
			G_OS_INFO_KEY_NAME,
			"$OS_RELEASE:NAME$",
			"this distribution",
		    },
		    {
			G_OS_INFO_KEY_BUG_REPORT_URL,
			"$OS_RELEASE:BUG_REPORT_URL$",
			"https://github.com/fwupd/fwupd/issues",
		    },
		};

		xpath = g_strdup_printf("component/id[text()='%s']/..", component_id);
		component = xb_silo_query_first(self->silo, xpath, NULL);
		if (component != NULL) {
			agreement_markup =
			    _fwupd_remote_get_agreement_for_app(remote, component, error);
		} else {
			agreement_markup = _fwupd_remote_get_agreement_default(remote, error);
		}
		if (agreement_markup == NULL)
			return FALSE;

		/* replace any dynamic values from os-release */
		for (guint i = 0; i < G_N_ELEMENTS(distro_kv); i++) {
			g_autofree gchar *os_replace = g_get_os_info(distro_kv[i].key);
			if (os_replace == NULL)
				os_replace = g_strdup(distro_kv[i].fallback);
			g_string_replace(agreement_markup, distro_kv[i].search, os_replace, 0);
		}
	}

	/* set mtime */
	fwupd_remote_set_mtime(remote, _fwupd_remote_get_mtime(remote));
	fu_remote_list_add_remote(self, remote);

	/* success */
	return TRUE;
}

static gboolean
fu_remote_list_add_for_path(FuRemoteList *self, const gchar *path, GError **error)
{
	g_autofree gchar *path_remotes = NULL;
	g_autoptr(GPtrArray) paths = NULL;

	path_remotes = g_build_filename(path, "remotes.d", NULL);
	if (!g_file_test(path_remotes, G_FILE_TEST_EXISTS)) {
		g_debug("path %s does not exist", path_remotes);
		return TRUE;
	}
	if (!fu_remote_list_add_inotify(self, path_remotes, error))
		return FALSE;
	paths = fu_path_glob(path_remotes, "*.conf", NULL);
	if (paths == NULL)
		return TRUE;
	for (guint i = 0; i < paths->len; i++) {
		const gchar *filename = g_ptr_array_index(paths, i);
		if (g_str_has_suffix(filename, "fwupd-tests.conf") && !self->testing_remote)
			continue;
		if (!fu_remote_list_add_for_file(self, filename, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
fu_remote_list_set_key_value(FuRemoteList *self,
			     const gchar *remote_id,
			     const gchar *key,
			     const gchar *value,
			     GError **error)
{
	FwupdRemote *remote;
	const gchar *filename;
	g_autofree gchar *filename_new = NULL;
	g_autofree gchar *value_old = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GKeyFile) keyfile = g_key_file_new();

	/* check remote is valid */
	remote = fu_remote_list_get_by_id(self, remote_id);
	if (remote == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "remote %s not found",
			    remote_id);
		return FALSE;
	}

	/* modify the remote */
	filename = fwupd_remote_get_filename_source(remote);
	if (!g_key_file_load_from_file(keyfile, filename, G_KEY_FILE_KEEP_COMMENTS, error)) {
		g_prefix_error(error, "failed to load %s: ", filename);
		return FALSE;
	}
	value_old = g_key_file_get_string(keyfile, "fwupd Remote", key, NULL);
	if (g_strcmp0(value_old, value) == 0)
		return TRUE;
	g_key_file_set_string(keyfile, "fwupd Remote", key, value);

	/* try existing file first, then call back to the mutable location */
	if (!g_key_file_save_to_file(keyfile, filename, &error_local)) {
		if (g_error_matches(error_local, G_FILE_ERROR, G_FILE_ERROR_PERM)) {
			g_autofree gchar *basename = g_path_get_basename(filename);
			g_autofree gchar *remotesdir_mut =
			    fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);

			filename_new =
			    g_build_filename(remotesdir_mut, "remotes.d", basename, NULL);
			if (!fu_path_mkdir_parent(filename_new, error))
				return FALSE;
			g_info("falling back from %s to %s", filename, filename_new);
			if (!g_key_file_save_to_file(keyfile, filename_new, error))
				return FALSE;
		} else {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	} else {
		filename_new = g_strdup(filename);
	}

	/* reload values */
	if (!fu_remote_load_from_filename(remote, filename_new, NULL, error)) {
		g_prefix_error(error, "failed to load %s: ", filename_new);
		return FALSE;
	}
	fu_remote_list_emit_changed(self);
	return TRUE;
}

static gint
fu_remote_list_sort_cb(gconstpointer a, gconstpointer b)
{
	FwupdRemote *remote_a = *((FwupdRemote **)a);
	FwupdRemote *remote_b = *((FwupdRemote **)b);

	/* use priority first */
	if (fwupd_remote_get_priority(remote_a) < fwupd_remote_get_priority(remote_b))
		return 1;
	if (fwupd_remote_get_priority(remote_a) > fwupd_remote_get_priority(remote_b))
		return -1;

	/* fall back to name */
	return g_strcmp0(fwupd_remote_get_id(remote_a), fwupd_remote_get_id(remote_b));
}

static guint
fu_remote_list_depsolve_order_before(FuRemoteList *self)
{
	guint cnt = 0;

	for (guint i = 0; i < self->array->len; i++) {
		FwupdRemote *remote = g_ptr_array_index(self->array, i);
		gchar **order = fwupd_remote_get_order_before(remote);
		if (order == NULL)
			continue;
		for (guint j = 0; order[j] != NULL; j++) {
			FwupdRemote *remote2;
			if (g_strcmp0(order[j], fwupd_remote_get_id(remote)) == 0) {
				g_debug("ignoring self-dep remote %s", order[j]);
				continue;
			}
			remote2 = fu_remote_list_get_by_id(self, order[j]);
			if (remote2 == NULL) {
				g_debug("ignoring unfound remote %s", order[j]);
				continue;
			}
			if (fwupd_remote_get_priority(remote) > fwupd_remote_get_priority(remote2))
				continue;
			g_debug("ordering %s=%s+1",
				fwupd_remote_get_id(remote),
				fwupd_remote_get_id(remote2));
			fwupd_remote_set_priority(remote, fwupd_remote_get_priority(remote2) + 1);
			cnt++;
		}
	}
	return cnt;
}

static guint
fu_remote_list_depsolve_order_after(FuRemoteList *self)
{
	guint cnt = 0;

	for (guint i = 0; i < self->array->len; i++) {
		FwupdRemote *remote = g_ptr_array_index(self->array, i);
		gchar **order = fwupd_remote_get_order_after(remote);
		if (order == NULL)
			continue;
		for (guint j = 0; order[j] != NULL; j++) {
			FwupdRemote *remote2;
			if (g_strcmp0(order[j], fwupd_remote_get_id(remote)) == 0) {
				g_debug("ignoring self-dep remote %s", order[j]);
				continue;
			}
			remote2 = fu_remote_list_get_by_id(self, order[j]);
			if (remote2 == NULL) {
				g_debug("ignoring unfound remote %s", order[j]);
				continue;
			}
			if (fwupd_remote_get_priority(remote) < fwupd_remote_get_priority(remote2))
				continue;
			g_debug("ordering %s=%s+1",
				fwupd_remote_get_id(remote2),
				fwupd_remote_get_id(remote));
			fwupd_remote_set_priority(remote2, fwupd_remote_get_priority(remote) + 1);
			cnt++;
		}
	}
	return cnt;
}

static gboolean
fu_remote_list_reload(FuRemoteList *self, GError **error)
{
	guint depsolve_check;
	g_autofree gchar *remotesdir = NULL;
	g_autofree gchar *remotesdir_mut = NULL;
	g_autofree gchar *remotesdir_immut = NULL;
	g_autoptr(GString) str = g_string_new(NULL);

	/* clear */
	g_ptr_array_set_size(self->array, 0);
	g_ptr_array_set_size(self->monitors, 0);

	/* search mutable, and then fall back to /etc and immutable */
	remotesdir_mut = fu_path_from_kind(FU_PATH_KIND_LOCALSTATEDIR_PKG);
	if (!fu_remote_list_add_for_path(self, remotesdir_mut, error))
		return FALSE;
	remotesdir = fu_path_from_kind(FU_PATH_KIND_SYSCONFDIR_PKG);
	if (!fu_remote_list_add_for_path(self, remotesdir, error))
		return FALSE;
	remotesdir_immut = fu_path_from_kind(FU_PATH_KIND_DATADIR_PKG);
	if (!fu_remote_list_add_for_path(self, remotesdir_immut, error))
		return FALSE;

	/* depsolve */
	for (depsolve_check = 0; depsolve_check < 100; depsolve_check++) {
		guint cnt = 0;
		cnt += fu_remote_list_depsolve_order_before(self);
		cnt += fu_remote_list_depsolve_order_after(self);
		if (cnt == 0)
			break;
	}
	if (depsolve_check == 100) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "Cannot depsolve remotes ordering");
		return FALSE;
	}

	/* order these by priority, then name */
	g_ptr_array_sort(self->array, fu_remote_list_sort_cb);

	/* print to the console */
	for (guint i = 0; i < self->array->len; i++) {
		FwupdRemote *remote = g_ptr_array_index(self->array, i);
		if (!fwupd_remote_has_flag(remote, FWUPD_REMOTE_FLAG_ENABLED))
			continue;
		if (str->len > 0)
			g_string_append(str, ", ");
		g_string_append_printf(str,
				       "%s[%i]",
				       fwupd_remote_get_id(remote),
				       fwupd_remote_get_priority(remote));
	}
	g_info("enabled remotes: %s", str->str);

	/* success */
	return TRUE;
}

static gboolean
fu_remote_list_load_metainfos(XbBuilder *builder, GError **error)
{
	const gchar *fn;
	g_autofree gchar *datadir = NULL;
	g_autofree gchar *metainfo_path = NULL;
	g_autoptr(GDir) dir = NULL;

	/* pkg metainfo dir */
	datadir = fu_path_from_kind(FU_PATH_KIND_DATADIR_PKG);
	metainfo_path = g_build_filename(datadir, "metainfo", NULL);
	if (!g_file_test(metainfo_path, G_FILE_TEST_EXISTS))
		return TRUE;

	g_debug("loading %s", metainfo_path);
	dir = g_dir_open(metainfo_path, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((fn = g_dir_read_name(dir)) != NULL) {
		if (g_str_has_suffix(fn, ".metainfo.xml")) {
			g_autofree gchar *filename = g_build_filename(metainfo_path, fn, NULL);
			g_autoptr(GFile) file = g_file_new_for_path(filename);
			g_autoptr(XbBuilderSource) source = xb_builder_source_new();
			if (!xb_builder_source_load_file(source,
							 file,
							 XB_BUILDER_SOURCE_FLAG_NONE,
							 NULL,
							 error))
				return FALSE;
			xb_builder_import_source(builder, source);
		}
	}
	return TRUE;
}

gboolean
fu_remote_list_set_testing_remote_enabled(FuRemoteList *self, gboolean enable, GError **error)
{
	g_return_val_if_fail(FU_IS_REMOTE_LIST(self), FALSE);

	/* not yet initialized */
	if (self->silo == NULL)
		return TRUE;

	if (self->testing_remote == enable)
		return TRUE;

	self->testing_remote = enable;

	if (!fu_remote_list_reload(self, error))
		return FALSE;

	fu_remote_list_emit_changed(self);

	return TRUE;
}

gboolean
fu_remote_list_load(FuRemoteList *self, FuRemoteListLoadFlags flags, GError **error)
{
	const gchar *const *locales = g_get_language_names();
	g_autoptr(GFile) xmlb = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new();
	XbBuilderCompileFlags compile_flags =
	    XB_BUILDER_COMPILE_FLAG_SINGLE_LANG | XB_BUILDER_COMPILE_FLAG_IGNORE_INVALID;

	g_return_val_if_fail(FU_IS_REMOTE_LIST(self), FALSE);
	g_return_val_if_fail(self->silo == NULL, FALSE);

	/* enable testing only remotes */
	if (flags & FU_REMOTE_LIST_LOAD_FLAG_TEST_REMOTE)
		self->testing_remote = TRUE;

	/* autofix on reload too */
	if (flags & FU_REMOTE_LIST_LOAD_FLAG_FIX_METADATA_URI)
		self->fix_metadata_uri = TRUE;

	/* load AppStream about the remote_list */
	if (!fu_remote_list_load_metainfos(builder, error))
		return FALSE;

	/* add the locales, which is really only going to be 'C' or 'en' */
	for (guint i = 0; locales[i] != NULL; i++)
		xb_builder_add_locale(builder, locales[i]);

	/* on a read-only filesystem don't care about the cache GUID */
	if (flags & FU_REMOTE_LIST_LOAD_FLAG_READONLY_FS)
		compile_flags |= XB_BUILDER_COMPILE_FLAG_IGNORE_GUID;

	/* build the metainfo silo */
	if (flags & FU_REMOTE_LIST_LOAD_FLAG_NO_CACHE) {
		g_autoptr(GFileIOStream) iostr = NULL;
		xmlb = g_file_new_tmp(NULL, &iostr, error);
		if (xmlb == NULL)
			return FALSE;
	} else {
		g_autofree gchar *cachedirpkg = fu_path_from_kind(FU_PATH_KIND_CACHEDIR_PKG);
		g_autofree gchar *xmlbfn = g_build_filename(cachedirpkg, "metainfo.xmlb", NULL);
		xmlb = g_file_new_for_path(xmlbfn);
	}
	self->silo = xb_builder_ensure(builder, xmlb, compile_flags, NULL, error);
	if (self->silo == NULL)
		return FALSE;

	/* load remote_list */
	return fu_remote_list_reload(self, error);
}

GPtrArray *
fu_remote_list_get_all(FuRemoteList *self)
{
	g_return_val_if_fail(FU_IS_REMOTE_LIST(self), NULL);
	return self->array;
}

FwupdRemote *
fu_remote_list_get_by_id(FuRemoteList *self, const gchar *remote_id)
{
	g_return_val_if_fail(FU_IS_REMOTE_LIST(self), NULL);
	for (guint i = 0; i < self->array->len; i++) {
		FwupdRemote *remote = g_ptr_array_index(self->array, i);
		if (g_strcmp0(remote_id, fwupd_remote_get_id(remote)) == 0)
			return remote;
	}
	return NULL;
}

static void
fu_remote_list_class_init(FuRemoteListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_remote_list_finalize;

	/**
	 * FuRemoteList::changed:
	 * @self: the #FuRemoteList instance that emitted the signal
	 *
	 * The ::changed signal is emitted when the list of remotes has
	 * changed, for instance when a remote has been added or removed.
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
	 * FuRemoteList::added:
	 * @self: the #FuRemoteList instance that emitted the signal
	 * @remote: the #FwupdRemote that was added
	 **/
	signals[SIGNAL_ADDED] = g_signal_new("added",
					     G_TYPE_FROM_CLASS(object_class),
					     G_SIGNAL_RUN_LAST,
					     0,
					     NULL,
					     NULL,
					     g_cclosure_marshal_generic,
					     G_TYPE_NONE,
					     1,
					     FWUPD_TYPE_REMOTE);
}

static void
fu_remote_list_monitor_unref(GFileMonitor *monitor)
{
	g_file_monitor_cancel(monitor);
	g_object_unref(monitor);
}

static void
fu_remote_list_init(FuRemoteList *self)
{
	self->array = g_ptr_array_new_with_free_func((GDestroyNotify)g_object_unref);
	self->monitors =
	    g_ptr_array_new_with_free_func((GDestroyNotify)fu_remote_list_monitor_unref);
}

static void
fu_remote_list_finalize(GObject *obj)
{
	FuRemoteList *self = FU_REMOTE_LIST(obj);
	if (self->silo != NULL)
		g_object_unref(self->silo);
	g_ptr_array_unref(self->array);
	g_ptr_array_unref(self->monitors);
	g_free(self->lvfs_metadata_format);
	G_OBJECT_CLASS(fu_remote_list_parent_class)->finalize(obj);
}

FuRemoteList *
fu_remote_list_new(void)
{
	FuRemoteList *self;
	self = g_object_new(FU_TYPE_REMOTE_LIST, NULL);
	return FU_REMOTE_LIST(self);
}

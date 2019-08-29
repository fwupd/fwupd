/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuConfig"

#include "config.h"

#include <xmlb.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <glib/gi18n.h>

#include "fu-common.h"
#include "fu-config.h"

#include "fwupd-common.h"
#include "fwupd-error.h"
#include "fwupd-remote-private.h"


enum {
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

static void fu_config_finalize	 (GObject *obj);

struct _FuConfig
{
	GObject			 parent_instance;
	GKeyFile		*keyfile;
	GPtrArray		*remotes;
	GPtrArray		*monitors;
	GPtrArray		*blacklist_devices;
	GPtrArray		*blacklist_plugins;
	GPtrArray		*approved_firmware;
	guint64			 archive_size_max;
	guint			 idle_timeout;
	XbSilo			*silo;
	GHashTable		*os_release;
	gchar			*config_file;
};

G_DEFINE_TYPE (FuConfig, fu_config, G_TYPE_OBJECT)

static void
fu_config_emit_changed (FuConfig *self)
{
	g_debug ("::configuration changed");
	g_signal_emit (self, signals[SIGNAL_CHANGED], 0);
}

static GPtrArray *
fu_config_get_config_paths (void)
{
	GPtrArray *paths = g_ptr_array_new_with_free_func (g_free);
	const gchar *remotes_dir;
	const gchar *system_prefixlibdir = "/usr/lib/fwupd";
	g_autofree gchar *configdir = NULL;

	/* only set by the self test program */
	remotes_dir = g_getenv ("FU_SELF_TEST_REMOTES_DIR");
	if (remotes_dir != NULL) {
		g_ptr_array_add (paths, g_strdup (remotes_dir));
		return paths;
	}

	/* use sysconfig, and then fall back to /etc */
	configdir = fu_common_get_path (FU_PATH_KIND_SYSCONFDIR_PKG);
	if (g_file_test (configdir, G_FILE_TEST_EXISTS))
		g_ptr_array_add (paths, g_steal_pointer (&configdir));

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
	if (!fu_config_load (self, FU_CONFIG_LOAD_FLAG_NONE, &error))
		g_warning ("failed to rescan config: %s", error->message);
	fu_config_emit_changed (self);
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

static GString *
fu_config_get_remote_agreement_default (FwupdRemote *self, GError **error)
{
	GString *str = g_string_new (NULL);

	/* this is designed as a fallback; the actual warning should ideally
	 * come from the LVFS instance that is serving the remote */
	g_string_append_printf (str, "%s\n",
				/* TRANSLATORS: show the user a warning */
				_("Your distributor may not have verified any of "
				  "the firmware updates for compatibility with your "
				  "system or connected devices."));
	g_string_append_printf (str, "%s\n",
				/* TRANSLATORS: show the user a warning */
				_("Enabling this remote is done at your own risk."));
	return str;
}

static GString *
fu_config_get_remote_agreement_for_app (FwupdRemote *self, XbNode *component, GError **error)
{
	XbNode *n;
	g_autofree gchar *tmp = NULL;
	g_autoptr(GError) error_local = NULL;

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
fu_config_build_remote_component_id (FwupdRemote *remote)
{
	return g_strdup_printf ("org.freedesktop.fwupd.remotes.%s",
				fwupd_remote_get_id (remote));
}

static gboolean
fu_config_add_remotes_for_path (FuConfig *self, const gchar *path, GError **error)
{
	const gchar *tmp;
	g_autofree gchar *path_remotes = NULL;
	g_autoptr(GDir) dir = NULL;

	path_remotes = g_build_filename (path, "remotes.d", NULL);
	if (!g_file_test (path_remotes, G_FILE_TEST_EXISTS)) {
		g_debug ("path %s does not exist", path_remotes);
		return TRUE;
	}
	if (!fu_config_add_inotify (self, path_remotes, error))
		return FALSE;
	dir = g_dir_open (path_remotes, 0, error);
	if (dir == NULL)
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

		/* try to find a custom agreement, falling back to a generic warning */
		if (fwupd_remote_get_kind (remote) == FWUPD_REMOTE_KIND_DOWNLOAD) {
			g_autoptr(GString) agreement_markup = NULL;
			g_autofree gchar *component_id = fu_config_build_remote_component_id (remote);
			g_autoptr(XbNode) component = NULL;
			g_autofree gchar *xpath = NULL;

			xpath = g_strdup_printf ("component/id[text()='%s']/..", component_id);
			component = xb_silo_query_first (self->silo, xpath, NULL);
			if (component != NULL) {
				agreement_markup = fu_config_get_remote_agreement_for_app (remote, component, error);
			} else {
				agreement_markup = fu_config_get_remote_agreement_default (remote, error);
			}
			if (agreement_markup == NULL)
				return FALSE;

			/* replace any dynamic values from os-release */
			tmp = g_hash_table_lookup (self->os_release, "NAME");
			if (tmp == NULL)
				tmp = "this distribution";
			fu_common_string_replace (agreement_markup, "$OS_RELEASE:NAME$", tmp);
			tmp = g_hash_table_lookup (self->os_release, "BUG_REPORT_URL");
			if (tmp == NULL)
				tmp = "https://github.com/fwupd/fwupd/issues";
			fu_common_string_replace (agreement_markup, "$OS_RELEASE:BUG_REPORT_URL$", tmp);
			fwupd_remote_set_agreement (remote, agreement_markup->str);
		}

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
fu_config_modify_and_save (FuConfig *self, const gchar *key, const gchar *value, GError **error)
{
	g_key_file_set_string (self->keyfile, "fwupd", key, value);
	return g_key_file_save_to_file (self->keyfile, self->config_file, error);
}

static gboolean
fu_config_load_from_file (FuConfig *self, const gchar *config_file,
			  GError **error)
{
	GFileMonitor *monitor;
	guint64 archive_size_max;
	guint idle_timeout;
	g_auto(GStrv) approved_firmware = NULL;
	g_auto(GStrv) devices = NULL;
	g_auto(GStrv) plugins = NULL;
	g_autoptr(GFile) file = NULL;
	g_autofree gchar *domains = NULL;

	/* ensure empty in case we're called from a monitor change */
	g_ptr_array_set_size (self->blacklist_devices, 0);
	g_ptr_array_set_size (self->blacklist_plugins, 0);
	g_ptr_array_set_size (self->approved_firmware, 0);
	g_ptr_array_set_size (self->monitors, 0);
	g_ptr_array_set_size (self->remotes, 0);

	g_debug ("loading config values from %s", config_file);
	if (!g_key_file_load_from_file (self->keyfile, config_file,
					G_KEY_FILE_KEEP_COMMENTS, error))
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

	/* get approved firmware */
	approved_firmware = g_key_file_get_string_list (self->keyfile,
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
	archive_size_max = g_key_file_get_uint64 (self->keyfile,
						  "fwupd",
						  "ArchiveSizeMax",
						  NULL);
	if (archive_size_max > 0)
		self->archive_size_max = archive_size_max *= 0x100000;

	/* get idle timeout */
	idle_timeout = g_key_file_get_uint64 (self->keyfile,
					      "fwupd",
					      "IdleTimeout",
					      NULL);
	if (idle_timeout > 0)
		self->idle_timeout = idle_timeout;

	/* get the domains to run in verbose */
	domains = g_key_file_get_string (self->keyfile,
					 "fwupd",
					 "VerboseDomains",
					 NULL);
	if (domains != NULL && domains[0] != '\0')
		g_setenv ("FWUPD_VERBOSE", domains, TRUE);

	return TRUE;
}

static gboolean
fu_config_load_metainfos (XbBuilder *builder, GError **error)
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
fu_config_load (FuConfig *self, FuConfigLoadFlags flags, GError **error)
{
	const gchar *const *locales = g_get_language_names ();
	g_autofree gchar *configdir = NULL;
	g_autofree gchar *cachedirpkg = NULL;
	g_autofree gchar *xmlbfn = NULL;
	g_autoptr(GFile) xmlb = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new ();
	XbBuilderCompileFlags compile_flags = XB_BUILDER_COMPILE_FLAG_SINGLE_LANG |
					      XB_BUILDER_COMPILE_FLAG_IGNORE_INVALID;

	g_return_val_if_fail (FU_IS_CONFIG (self), FALSE);

	/* load the main daemon config file */
	configdir = fu_common_get_path (FU_PATH_KIND_SYSCONFDIR_PKG);
	self->config_file = g_build_filename (configdir, "daemon.conf", NULL);
	if (g_file_test (self->config_file, G_FILE_TEST_EXISTS)) {
		if (!fu_config_load_from_file (self, self->config_file, error))
			return FALSE;
	} else {
		g_warning ("Daemon configuration %s not found", self->config_file);
	}

	/* load AppStream about the remotes */
	self->os_release = fwupd_get_os_release (error);
	if (self->os_release == NULL)
		return FALSE;
	if (!fu_config_load_metainfos (builder, error))
		return FALSE;

	/* add the locales, which is really only going to be 'C' or 'en' */
	for (guint i = 0; locales[i] != NULL; i++)
		xb_builder_add_locale (builder, locales[i]);

#if LIBXMLB_CHECK_VERSION(0,1,7)
	/* on a read-only filesystem don't care about the cache GUID */
	if (flags & FU_CONFIG_LOAD_FLAG_READONLY_FS)
		compile_flags |= XB_BUILDER_COMPILE_FLAG_IGNORE_GUID;
#endif

	/* build the metainfo silo */
	cachedirpkg = fu_common_get_path (FU_PATH_KIND_CACHEDIR_PKG);
	xmlbfn = g_build_filename (cachedirpkg, "metainfo.xmlb", NULL);
	xmlb = g_file_new_for_path (xmlbfn);
	self->silo = xb_builder_ensure (builder, xmlb, compile_flags, NULL, error);
	if (self->silo == NULL)
		return FALSE;

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

guint
fu_config_get_idle_timeout (FuConfig *self)
{
	g_return_val_if_fail (FU_IS_CONFIG (self), 0);
	return self->idle_timeout;
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
	self->keyfile = g_key_file_new ();
	self->blacklist_devices = g_ptr_array_new_with_free_func (g_free);
	self->blacklist_plugins = g_ptr_array_new_with_free_func (g_free);
	self->approved_firmware = g_ptr_array_new_with_free_func (g_free);
	self->remotes = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	self->monitors = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
}

static void
fu_config_finalize (GObject *obj)
{
	FuConfig *self = FU_CONFIG (obj);

	if (self->os_release != NULL)
		g_hash_table_unref (self->os_release);
	if (self->silo != NULL)
		g_object_unref (self->silo);
	g_key_file_unref (self->keyfile);
	g_ptr_array_unref (self->blacklist_devices);
	g_ptr_array_unref (self->blacklist_plugins);
	g_ptr_array_unref (self->approved_firmware);
	g_ptr_array_unref (self->remotes);
	g_ptr_array_unref (self->monitors);
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

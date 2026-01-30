/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuPathStore"

#include "config.h"

#include "fu-path-store.h"

struct _FuPathStore {
	GObject parent_instance;
	gchar *paths[FU_PATH_KIND_LAST];
	gboolean loaded_defaults;
	gboolean loaded_from_env;
};

G_DEFINE_TYPE(FuPathStore, fu_path_store, G_TYPE_OBJECT)

/**
 * fu_path_store_get_path:
 * @self: a #FuPathStore
 * @kind: a #FuPathKind e.g. %FU_PATH_KIND_DATADIR_PKG
 * @error: (nullable): optional return location for an error
 *
 * Gets the defined path for the @kind.
 *
 * Returns: filename, or %NULL
 *
 * Since: 2.1.1
 **/
const gchar *
fu_path_store_get_path(FuPathStore *self, FuPathKind kind, GError **error)
{
	g_return_val_if_fail(FU_IS_PATH_STORE(self), NULL);
	g_return_val_if_fail(kind < FU_PATH_KIND_LAST, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	if (self->paths[kind] == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "no path set for %s",
			    fu_path_kind_to_string(kind));
		return NULL;
	}
	return self->paths[kind];
}

/**
 * fu_path_store_build_filename:
 * @self: a #FuPathStore
 * @error: (nullable): optional return location for an error
 * @kind: a #FuPathKind e.g. %FU_PATH_KIND_DATADIR_PKG
 * @...: pairs of string key values, ending with %NULL
 *
 * Gets a fwupd-specific system path. These can be overridden with various
 * environment variables, for instance %FWUPD_DATADIR.
 *
 * Returns: a system path, or %NULL if invalid
 *
 * Since: 2.1.1
 **/
gchar *
fu_path_store_build_filename(FuPathStore *self, GError **error, FuPathKind kind, ...)
{
	va_list args;
	gchar *path;
	const gchar *path_prefix = NULL;

	g_return_val_if_fail(FU_IS_PATH_STORE(self), NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);
	g_return_val_if_fail(kind < FU_PATH_KIND_LAST, NULL);

	path_prefix = fu_path_store_get_path(self, kind, error);
	if (path_prefix == NULL)
		return NULL;

	va_start(args, kind);
	path = g_build_filename_valist(path_prefix, &args);
	va_end(args);

	return path;
}

/**
 * fu_path_store_set_path:
 * @self: a #FuPathStore
 * @kind: a #FuPathKind e.g. %FU_PATH_KIND_DATADIR_PKG
 * @path: (nullable): directory name
 *
 * Sets the defined path for the @kind.
 *
 * Since: 2.1.1
 **/
void
fu_path_store_set_path(FuPathStore *self, FuPathKind kind, const gchar *path)
{
	g_return_if_fail(FU_IS_PATH_STORE(self));
	g_return_if_fail(kind < FU_PATH_KIND_LAST);
	g_free(self->paths[kind]);
	self->paths[kind] = g_strdup(path);
}

/**
 * fu_path_store_set_tmpdir:
 * @self: a #FuPathStore
 * @kind: a #FuPathKind e.g. %FU_PATH_KIND_DATADIR_PKG
 * @tmpdir: (not nullable): a #FuTemporaryDirectory
 *
 * Sets the @kind to a temporary path.
 *
 * Since: 2.1.1
 **/
void
fu_path_store_set_tmpdir(FuPathStore *self, FuPathKind kind, FuTemporaryDirectory *tmpdir)
{
	g_return_if_fail(FU_IS_PATH_STORE(self));
	g_return_if_fail(kind < FU_PATH_KIND_LAST);
	g_return_if_fail(FU_IS_TEMPORARY_DIRECTORY(tmpdir));
	fu_path_store_set_path(self, kind, fu_temporary_directory_get_path(tmpdir));
}

/**
 * fu_path_store_add_prefix:
 * @self: a #FuPathStore
 * @prefix: (not nullable): directory name
 *
 * Sets the prefix path for the @kind.
 *
 * Since: 2.1.1
 **/
void
fu_path_store_add_prefix(FuPathStore *self, FuPathKind kind, const gchar *prefix)
{
	gchar *path;
	g_return_if_fail(FU_IS_PATH_STORE(self));
	g_return_if_fail(kind < FU_PATH_KIND_LAST);
	g_return_if_fail(prefix != NULL);

	if (self->paths[kind] == NULL)
		return;
	path = g_build_filename(prefix, self->paths[kind], NULL);
	g_free(self->paths[kind]);
	self->paths[kind] = path;
}

static void
fu_path_store_add_dir(FuPathStore *self, FuPathKind kind, const gchar *first, ...)
{
	va_list args;
	g_autofree gchar *dir = NULL;

	va_start(args, first);
	dir = g_build_filename_valist(first, &args);
	va_end(args);

	fu_path_store_set_path(self, kind, dir);
}

static void
fu_path_store_ensure_lockdir(FuPathStore *self)
{
	const gchar *dirs[] = {
	    "/run/lock",
	    "/var/run",
	};
	for (guint i = 0; i < G_N_ELEMENTS(dirs); i++) {
		if (g_file_test(dirs[i], G_FILE_TEST_EXISTS)) {
			fu_path_store_set_path(self, FU_PATH_KIND_LOCKDIR, dirs[i]);
			break;
		}
	}
}

static void
fu_path_store_ensure_localtime(FuPathStore *self)
{
	const gchar *dirs[] = {
	    "/var/lib/timezone/localtime",
	    FWUPD_SYSCONFDIR "/localtime",
	    "/etc/localtime",
	};
	for (guint i = 0; i < G_N_ELEMENTS(dirs); i++) {
		g_debug("looking for %s", dirs[i]);
		if (g_file_test(dirs[i], G_FILE_TEST_EXISTS)) {
			fu_path_store_set_path(self, FU_PATH_KIND_LOCALTIME, dirs[i]);
			break;
		}
	}
}

#ifdef _WIN32
static gchar *
fu_path_store_get_win32_basedir(void)
{
	char drive_buf[_MAX_DRIVE];
	char dir_buf[_MAX_DIR];
	_splitpath(_pgmptr, drive_buf, dir_buf, NULL, NULL);
	return g_build_filename(drive_buf, dir_buf, "..", NULL);
}
#endif

/**
 * fu_path_store_load_defaults:
 * @self: a #FuPathStore
 *
 * Load the default paths for a typical system.
 *
 * Since: 2.1.1
 **/
void
fu_path_store_load_defaults(FuPathStore *self)
{
#ifdef _WIN32
	g_autofree gchar *win32_basedir = fu_path_store_get_win32_basedir();
#endif

	g_return_if_fail(FU_IS_PATH_STORE(self));

	/* already done */
	if (self->loaded_defaults)
		return;

	/* hardcoded */
	fu_path_store_set_path(self, FU_PATH_KIND_HOSTFS_ROOT, "/");
	fu_path_store_set_path(self, FU_PATH_KIND_HOSTFS_BOOT, "/boot");
	fu_path_store_set_path(self, FU_PATH_KIND_PROCFS, "/proc");
	fu_path_store_set_path(self, FU_PATH_KIND_DEVFS, "/dev");
	fu_path_store_set_path(self, FU_PATH_KIND_RUNDIR, "/run");
	fu_path_store_set_path(self, FU_PATH_KIND_SYSFSDIR, "/sys");
	fu_path_store_set_path(self, FU_PATH_KIND_SYSFSDIR_FW, "/sys/firmware");
	fu_path_store_set_path(self, FU_PATH_KIND_SYSFSDIR_TPM, "/sys/class/tpm");
	fu_path_store_set_path(self, FU_PATH_KIND_SYSFSDIR_DRIVERS, "/sys/bus/platform/drivers");
	fu_path_store_set_path(self, FU_PATH_KIND_SYSFSDIR_SECURITY, "/sys/kernel/security");
	fu_path_store_set_path(self, FU_PATH_KIND_SYSFSDIR_DMI, "/sys/class/dmi/id");
	fu_path_store_set_path(self, FU_PATH_KIND_ACPI_TABLES, "/sys/firmware/acpi/tables");
	fu_path_store_set_path(self,
			       FU_PATH_KIND_FIRMWARE_SEARCH,
			       "/sys/module/firmware_class/parameters/path");
	fu_path_store_set_path(self,
			       FU_PATH_KIND_SYSFSDIR_FW_ATTRIB,
			       "/sys/class/firmware-attributes");
	fu_path_store_set_path(self, FU_PATH_KIND_DEBUGFSDIR, "/sys/kernel/debug");

	/* defined from the buildsystem */
	fu_path_store_set_path(self, FU_PATH_KIND_LOCALSTATEDIR, FWUPD_LOCALSTATEDIR);
	fu_path_store_set_path(self, FU_PATH_KIND_LIBEXECDIR, FWUPD_LIBEXECDIR);
	fu_path_store_add_dir(self,
			      FU_PATH_KIND_LIBEXECDIR_PKG,
			      FWUPD_LIBEXECDIR,
			      PACKAGE_NAME,
			      NULL);
	fu_path_store_set_path(self, FU_PATH_KIND_DATADIR_VENDOR_IDS, FWUPD_DATADIR_VENDOR_IDS);
	fu_path_store_add_dir(self,
			      FU_PATH_KIND_LOCALSTATEDIR_PKG,
			      FWUPD_LOCALSTATEDIR,
			      "lib",
			      PACKAGE_NAME,
			      NULL);
	fu_path_store_add_dir(self,
			      FU_PATH_KIND_LOCALSTATEDIR_QUIRKS,
			      FWUPD_LOCALSTATEDIR,
			      "lib",
			      PACKAGE_NAME,
			      "quirks.d",
			      NULL);
	fu_path_store_add_dir(self,
			      FU_PATH_KIND_LOCALSTATEDIR_METADATA,
			      FWUPD_LOCALSTATEDIR,
			      "lib",
			      PACKAGE_NAME,
			      "metadata",
			      NULL);
	fu_path_store_add_dir(self,
			      FU_PATH_KIND_LOCALSTATEDIR_REMOTES,
			      FWUPD_LOCALSTATEDIR,
			      "lib",
			      PACKAGE_NAME,
			      "remotes.d",
			      NULL);
	fu_path_store_add_dir(self,
			      FU_PATH_KIND_CACHEDIR_PKG,
			      FWUPD_LOCALSTATEDIR,
			      "cache",
			      PACKAGE_NAME,
			      NULL);
	fu_path_store_add_dir(self,
			      FU_PATH_KIND_LOCALCONFDIR_PKG,
			      FWUPD_LOCALSTATEDIR,
			      "etc",
			      PACKAGE_NAME,
			      NULL);
	fu_path_store_set_path(self, FU_PATH_KIND_SYSCONFDIR, FWUPD_SYSCONFDIR);
	fu_path_store_add_dir(self,
			      FU_PATH_KIND_SYSCONFDIR_PKG,
			      FWUPD_SYSCONFDIR,
			      PACKAGE_NAME,
			      NULL);
	fu_path_store_set_path(self, FU_PATH_KIND_LIBDIR_PKG, FWUPD_LIBDIR_PKG);
	fu_path_store_add_dir(self, FU_PATH_KIND_DATADIR_PKG, FWUPD_DATADIR, PACKAGE_NAME, NULL);
	fu_path_store_add_dir(self,
			      FU_PATH_KIND_DATADIR_QUIRKS,
			      FWUPD_DATADIR,
			      PACKAGE_NAME,
			      "quirks.d",
			      NULL);
#ifdef EFI_APP_LOCATION
	fu_path_store_set_path(self, FU_PATH_KIND_EFIAPPDIR, EFI_APP_LOCATION);
#endif

	/* discovered from the filesystem */
	fu_path_store_ensure_lockdir(self);
	fu_path_store_ensure_localtime(self);

#ifdef _WIN32
	/* fix up WIN32 */
	fu_path_store_set_path(self, FU_PATH_KIND_WIN32_BASEDIR, win32_basedir);
	fu_path_store_add_prefix(self, FU_PATH_KIND_SYSCONFDIR, win32_basedir);
	fu_path_store_add_prefix(self, FU_PATH_KIND_SYSCONFDIR_PKG, win32_basedir);
	fu_path_store_add_prefix(self, FU_PATH_KIND_LIBDIR_PKG, win32_basedir);
	fu_path_store_add_prefix(self, FU_PATH_KIND_DATADIR_PKG, win32_basedir);
	fu_path_store_add_prefix(self, FU_PATH_KIND_DATADIR_QUIRKS, win32_basedir);
#endif

	/* success */
	self->loaded_defaults = TRUE;
}

/**
 * fu_path_store_load_from_env:
 * @self: a #FuPathStore
 *
 * Apply environment-based overrides to the existing paths.
 *
 * Since: 2.1.1
 **/
void
fu_path_store_load_from_env(FuPathStore *self)
{
	const gchar *tmp;
	struct {
		const gchar *env;
		FuPathKind kind;
	} envmap[] = {
	    {"CACHE_DIRECTORY", FU_PATH_KIND_CACHEDIR_PKG},
	    {"CONFIGURATION_DIRECTORY", FU_PATH_KIND_SYSCONFDIR_PKG},
	    {"LOCALCONF_DIRECTORY", FU_PATH_KIND_LOCALCONFDIR_PKG},
	    {"STATE_DIRECTORY", FU_PATH_KIND_LOCALSTATEDIR_PKG},
	    {"FWUPD_HOSTFS_ROOT", FU_PATH_KIND_HOSTFS_ROOT},
	    {"FWUPD_LIBDIR_PKG", FU_PATH_KIND_LIBDIR_PKG},
	    {"FWUPD_LOCKDIR", FU_PATH_KIND_LOCKDIR},
	    {"FWUPD_SYSFSFWATTRIBDIR", FU_PATH_KIND_SYSFSDIR_FW_ATTRIB},
	    {"FWUPD_SYSFSFWDIR", FU_PATH_KIND_SYSFSDIR_FW},
	};

	g_return_if_fail(FU_IS_PATH_STORE(self));

	/* already done */
	if (self->loaded_from_env)
		return;

	/* special cases */
	tmp = g_getenv("FWUPD_LOCALSTATEDIR");
	if (tmp != NULL) {
		fu_path_store_set_path(self, FU_PATH_KIND_LOCALSTATEDIR, tmp);
		fu_path_store_add_dir(self,
				      FU_PATH_KIND_LOCALSTATEDIR_PKG,
				      tmp,
				      "lib",
				      PACKAGE_NAME,
				      NULL);
		fu_path_store_add_dir(self,
				      FU_PATH_KIND_LOCALSTATEDIR_QUIRKS,
				      tmp,
				      "lib",
				      PACKAGE_NAME,
				      "quirks.d",
				      NULL);
		fu_path_store_add_dir(self,
				      FU_PATH_KIND_LOCALSTATEDIR_METADATA,
				      tmp,
				      "lib",
				      PACKAGE_NAME,
				      "metadata",
				      NULL);
		fu_path_store_add_dir(self,
				      FU_PATH_KIND_LOCALSTATEDIR_REMOTES,
				      tmp,
				      "lib",
				      PACKAGE_NAME,
				      "remotes.d",
				      NULL);
		fu_path_store_add_dir(self,
				      FU_PATH_KIND_LOCALCONFDIR_PKG,
				      tmp,
				      "etc",
				      PACKAGE_NAME,
				      NULL);
	}
	tmp = g_getenv("FWUPD_DATADIR");
	if (tmp != NULL) {
		fu_path_store_set_path(self, FU_PATH_KIND_DATADIR_PKG, tmp);
		fu_path_store_add_dir(self, FU_PATH_KIND_DATADIR_QUIRKS, tmp, "quirks.d", NULL);
	}
	tmp = g_getenv("FWUPD_SYSCONFDIR");
	if (tmp != NULL) {
		fu_path_store_set_path(self, FU_PATH_KIND_SYSCONFDIR, tmp);
		fu_path_store_add_dir(self, FU_PATH_KIND_SYSCONFDIR_PKG, tmp, PACKAGE_NAME, NULL);
	}
	tmp = g_getenv("FWUPD_LIBEXECDIR");
	if (tmp != NULL) {
		fu_path_store_set_path(self, FU_PATH_KIND_LIBEXECDIR, tmp);
		fu_path_store_add_dir(self, FU_PATH_KIND_LIBEXECDIR_PKG, tmp, PACKAGE_NAME, NULL);
	}

	for (guint i = 0; i < G_N_ELEMENTS(envmap); i++) {
		tmp = g_getenv(envmap[i].env);
		if (tmp != NULL)
			fu_path_store_set_path(self, envmap[i].kind, tmp);
	}

#ifdef _WIN32
	/* WIN32 special case */
	tmp = g_getenv("USERPROFILE");
	if (tmp != NULL) {
		fu_path_store_add_dir(self,
				      FU_PATH_KIND_LOCALSTATEDIR,
				      tmp,
				      PACKAGE_NAME,
				      FWUPD_LOCALSTATEDIR,
				      NULL);
	}
#endif

	/* snap special case */
	tmp = g_getenv("SNAP_COMMON");
	if (tmp != NULL) {
		fu_path_store_add_prefix(self, FU_PATH_KIND_LOCALSTATEDIR, tmp);
		fu_path_store_add_prefix(self, FU_PATH_KIND_LOCALSTATEDIR_METADATA, tmp);
		fu_path_store_add_prefix(self, FU_PATH_KIND_LOCALSTATEDIR_QUIRKS, tmp);
		fu_path_store_add_prefix(self, FU_PATH_KIND_LOCALSTATEDIR_REMOTES, tmp);
	}

	/* snap usual case */
	tmp = g_getenv("SNAP");
	if (tmp != NULL) {
		fu_path_store_add_prefix(self, FU_PATH_KIND_DATADIR_PKG, tmp);
		fu_path_store_add_prefix(self, FU_PATH_KIND_DATADIR_QUIRKS, tmp);
		fu_path_store_add_prefix(self, FU_PATH_KIND_DATADIR_VENDOR_IDS, tmp);
		fu_path_store_add_prefix(self, FU_PATH_KIND_EFIAPPDIR, tmp);
		fu_path_store_add_prefix(self, FU_PATH_KIND_LIBDIR_PKG, tmp);
		fu_path_store_add_prefix(self, FU_PATH_KIND_LIBEXECDIR_PKG, tmp);
		fu_path_store_add_prefix(self, FU_PATH_KIND_LIBEXECDIR, tmp);
		fu_path_store_add_prefix(self, FU_PATH_KIND_SYSCONFDIR_PKG, tmp);
		fu_path_store_add_prefix(self, FU_PATH_KIND_SYSCONFDIR, tmp);
	}

	/* success */
	self->loaded_from_env = TRUE;
}

static void
fu_path_store_finalize(GObject *object)
{
	FuPathStore *self = FU_PATH_STORE(object);
	for (guint i = 0; i < FU_PATH_KIND_LAST; i++)
		g_free(self->paths[i]);
	G_OBJECT_CLASS(fu_path_store_parent_class)->finalize(object);
}

static void
fu_path_store_class_init(FuPathStoreClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_path_store_finalize;
}

static void
fu_path_store_init(FuPathStore *self)
{
}

/**
 * fu_path_store_new:
 *
 * Returns: (transfer full): a #FuPathStore
 *
 * Since: 2.1.1
 **/
FuPathStore *
fu_path_store_new(void)
{
	return g_object_new(FU_TYPE_PATH_STORE, NULL);
}

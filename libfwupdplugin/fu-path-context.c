/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuPathContext"

#include "config.h"

#include "fu-path-context.h"

struct _FuPathContext {
	GObject parent_instance;
	gchar *dirnames[FU_PATH_KIND_LAST];
};

G_DEFINE_TYPE(FuPathContext, fu_path_context, G_TYPE_OBJECT)

/**
 * fu_path_context_get_dir:
 * @self: a #FuPathContext
 *
 * Gets the defined path for the @kind.
 *
 * Returns: filename, or %NULL
 *
 * Since: 2.1.1
 **/
const gchar *
fu_path_context_get_dir(FuPathContext *self, FuPathKind kind)
{
	g_return_val_if_fail(FU_IS_PATH_CONTEXT(self), NULL);
	g_return_val_if_fail(kind < FU_PATH_KIND_LAST, NULL);
	return self->dirnames[kind];
}

/**
 * fu_path_context_set_dir:
 * @self: a #FuPathContext
 * @dirname: (nullable): directory name
 *
 * Sets the defined path for the @kind.
 *
 * Since: 2.1.1
 **/
void
fu_path_context_set_dir(FuPathContext *self, FuPathKind kind, const gchar *dirname)
{
	g_return_if_fail(FU_IS_PATH_CONTEXT(self));
	g_return_if_fail(kind < FU_PATH_KIND_LAST);
	g_free(self->dirnames[kind]);
	self->dirnames[kind] = g_strdup(dirname);
}

/**
 * fu_path_context_add_prefix:
 * @self: a #FuPathContext
 * @prefix: (nullable): directory name
 *
 * Sets the prefix path for the @kind.
 *
 * Since: 2.1.1
 **/
void
fu_path_context_add_prefix(FuPathContext *self, FuPathKind kind, const gchar *prefix)
{
	gchar *dirname;
	g_return_if_fail(FU_IS_PATH_CONTEXT(self));
	g_return_if_fail(kind < FU_PATH_KIND_LAST);
	g_return_if_fail(prefix != NULL);

	if (self->dirnames[kind] == NULL)
		return;
	dirname = g_build_filename(prefix, self->dirnames[kind], NULL);
	g_free(self->dirnames[kind]);
	self->dirnames[kind] = dirname;
}

void
fu_path_context_build_dir(FuPathContext *self, FuPathKind kind, ...)
{
	va_list args;
	g_autofree gchar *dir = NULL;

	g_return_if_fail(FU_IS_PATH_CONTEXT(self));
	g_return_if_fail(kind < FU_PATH_KIND_LAST);

	va_start(args, kind);
	dir = g_build_filename_valist("/", &args);
	va_end(args);

	fu_path_context_set_dir(self, kind, dir);
}

static void
fu_path_context_ensure_lockdir(FuPathContext *self)
{
	const gchar *dirs[] = {
	    "/run/lock",
	    "/var/run",
	};
	for (guint i = 0; i < G_N_ELEMENTS(dirs); i++) {
		if (g_file_test(dirs[i], G_FILE_TEST_EXISTS)) {
			fu_path_context_set_dir(self, FU_PATH_KIND_LOCKDIR, dirs[i]);
			break;
		}
	}
}

static void
fu_path_context_ensure_localtime(FuPathContext *self)
{
	const gchar *dirs[] = {
	    "/var/lib/timezone/localtime",
	    FWUPD_SYSCONFDIR "/localtime",
	    "/etc/localtime",
	};
	for (guint i = 0; i < G_N_ELEMENTS(dirs); i++) {
		g_debug("looking for %s", dirs[i]);
		if (g_file_test(dirs[i], G_FILE_TEST_EXISTS)) {
			fu_path_context_set_dir(self, FU_PATH_KIND_LOCALTIME, dirs[i]);
			break;
		}
	}
}

#ifdef _WIN32
static gchar *
fu_path_context_get_win32_basedir(void)
{
	char drive_buf[_MAX_DRIVE];
	char dir_buf[_MAX_DIR];
	_splitpath(_pgmptr, drive_buf, dir_buf, NULL, NULL);
	return g_build_filename(drive_buf, dir_buf, "..", NULL);
}
#endif

/**
 * fu_path_context_load_defaults:
 * @self: a #FuPathContext
 *
 * Load the default paths for a typical system.
 *
 * Since: 2.1.1
 **/
void
fu_path_context_load_defaults(FuPathContext *self)
{
#ifdef _WIN32
	g_autofree gchar *win32_basedir = fu_path_context_get_win32_basedir();
#endif

	g_return_if_fail(FU_IS_PATH_CONTEXT(self));

	/* hardcoded */
	fu_path_context_set_dir(self, FU_PATH_KIND_HOSTFS_ROOT, "/");
	fu_path_context_set_dir(self, FU_PATH_KIND_HOSTFS_BOOT, "/boot");
	fu_path_context_set_dir(self, FU_PATH_KIND_PROCFS, "/proc");
	fu_path_context_set_dir(self, FU_PATH_KIND_DEVFS, "/dev");
	fu_path_context_set_dir(self, FU_PATH_KIND_RUNDIR, "/run");
	fu_path_context_set_dir(self, FU_PATH_KIND_SYSFSDIR, "/sys");
	fu_path_context_set_dir(self, FU_PATH_KIND_SYSFSDIR_FW, "/sys/firmware");
	fu_path_context_set_dir(self, FU_PATH_KIND_SYSFSDIR_TPM, "/sys/class/tpm");
	fu_path_context_set_dir(self, FU_PATH_KIND_SYSFSDIR_DRIVERS, "/sys/bus/platform/drivers");
	fu_path_context_set_dir(self, FU_PATH_KIND_SYSFSDIR_SECURITY, "/sys/kernel/security");
	fu_path_context_set_dir(self, FU_PATH_KIND_SYSFSDIR_DMI, "/sys/class/dmi/id");
	fu_path_context_set_dir(self, FU_PATH_KIND_ACPI_TABLES, "/sys/firmware/acpi/tables");
	fu_path_context_set_dir(self,
				FU_PATH_KIND_FIRMWARE_SEARCH,
				"/sys/module/firmware_class/parameters/path");
	fu_path_context_set_dir(self,
				FU_PATH_KIND_SYSFSDIR_FW_ATTRIB,
				"/sys/class/firmware-attributes");
	fu_path_context_set_dir(self, FU_PATH_KIND_DEBUGFSDIR, "/sys/kernel/debug");

	/* defined from the buildsystem */
	fu_path_context_set_dir(self, FU_PATH_KIND_LOCALSTATEDIR, FWUPD_LOCALSTATEDIR);
	fu_path_context_set_dir(self, FU_PATH_KIND_LIBEXECDIR, FWUPD_LIBEXECDIR);
	fu_path_context_build_dir(self,
				  FU_PATH_KIND_LIBEXECDIR_PKG,
				  FWUPD_LIBEXECDIR,
				  PACKAGE_NAME,
				  NULL);
	fu_path_context_set_dir(self, FU_PATH_KIND_DATADIR_VENDOR_IDS, FWUPD_DATADIR_VENDOR_IDS);
	fu_path_context_build_dir(self,
				  FU_PATH_KIND_LOCALSTATEDIR_PKG,
				  FWUPD_LOCALSTATEDIR,
				  "lib",
				  PACKAGE_NAME,
				  NULL);
	fu_path_context_build_dir(self,
				  FU_PATH_KIND_LOCALSTATEDIR_QUIRKS,
				  FWUPD_LOCALSTATEDIR,
				  "lib",
				  PACKAGE_NAME,
				  "quirks.d",
				  NULL);
	fu_path_context_build_dir(self,
				  FU_PATH_KIND_LOCALSTATEDIR_METADATA,
				  FWUPD_LOCALSTATEDIR,
				  "lib",
				  PACKAGE_NAME,
				  "metadata",
				  NULL);
	fu_path_context_build_dir(self,
				  FU_PATH_KIND_LOCALSTATEDIR_REMOTES,
				  FWUPD_LOCALSTATEDIR,
				  "lib",
				  PACKAGE_NAME,
				  "remotes.d",
				  NULL);
	fu_path_context_build_dir(self,
				  FU_PATH_KIND_CACHEDIR_PKG,
				  FWUPD_LOCALSTATEDIR,
				  "cache",
				  PACKAGE_NAME,
				  NULL);
	fu_path_context_build_dir(self,
				  FU_PATH_KIND_LOCALCONFDIR_PKG,
				  FWUPD_LOCALSTATEDIR,
				  "etc",
				  PACKAGE_NAME,
				  NULL);
	fu_path_context_set_dir(self, FU_PATH_KIND_SYSCONFDIR, FWUPD_SYSCONFDIR);
	fu_path_context_build_dir(self,
				  FU_PATH_KIND_SYSCONFDIR_PKG,
				  FWUPD_SYSCONFDIR,
				  PACKAGE_NAME,
				  NULL);
	fu_path_context_set_dir(self, FU_PATH_KIND_LIBDIR_PKG, FWUPD_LIBDIR_PKG);
	fu_path_context_build_dir(self,
				  FU_PATH_KIND_DATADIR_PKG,
				  FWUPD_DATADIR,
				  PACKAGE_NAME,
				  NULL);
	fu_path_context_build_dir(self,
				  FU_PATH_KIND_DATADIR_QUIRKS,
				  FWUPD_DATADIR,
				  PACKAGE_NAME,
				  "quirks.d",
				  NULL);
#ifdef EFI_APP_LOCATION
	fu_path_context_set_dir(self, FU_PATH_KIND_EFIAPPDIR, EFI_APP_LOCATION);
#endif
#ifdef POLKIT_ACTIONDIR
	fu_path_context_set_dir(self, FU_PATH_KIND_POLKIT_ACTIONS, POLKIT_ACTIONDIR);
#endif

	/* discovered from the filesystem */
	fu_path_context_ensure_lockdir(self);
	fu_path_context_ensure_localtime(self);

#ifdef _WIN32
	/* fix up WIN32 */
	fu_path_context_set_dir(self, FU_PATH_KIND_WIN32_BASEDIR, win32_basedir);
	fu_path_context_add_prefix(self, FU_PATH_KIND_SYSCONFDIR, win32_basedir);
	fu_path_context_add_prefix(self, FU_PATH_KIND_SYSCONFDIR_PKG, win32_basedir);
	fu_path_context_add_prefix(self, FU_PATH_KIND_LIBDIR_PKG, win32_basedir);
	fu_path_context_add_prefix(self, FU_PATH_KIND_DATADIR_PKG, win32_basedir);
	fu_path_context_add_prefix(self, FU_PATH_KIND_DATADIR_QUIRKS, win32_basedir);
#endif
}

/**
 * fu_path_context_load_from_env:
 * @self: a #FuPathContext
 *
 * Load the default paths for a typical system.
 *
 * Since: 2.1.1
 **/
void
fu_path_context_load_from_env(FuPathContext *self)
{
	const gchar *tmp;
	struct {
		const gchar *env;
		FuPathKind kind;
	} envmap[] = {
	    /* TODO: remove some (all?) of these once the self tests are using pathctx */
	    {"FWUPD_LOCALSTATEDIR", FU_PATH_KIND_LOCALSTATEDIR},
	    {"FWUPD_PROCFS", FU_PATH_KIND_PROCFS},
	    {"FWUPD_SYSFSDIR", FU_PATH_KIND_SYSFSDIR},
	    {"FWUPD_SYSFSFWDIR", FU_PATH_KIND_SYSFSDIR_FW},
	    {"FWUPD_SYSFSTPMDIR", FU_PATH_KIND_SYSFSDIR_TPM},
	    {"FWUPD_SYSFSDRIVERDIR", FU_PATH_KIND_SYSFSDIR_DRIVERS},
	    {"FWUPD_SYSFSSECURITYDIR", FU_PATH_KIND_SYSFSDIR_SECURITY},
	    {"FWUPD_SYSFSDMIDIR", FU_PATH_KIND_SYSFSDIR_DMI},
	    {"FWUPD_ACPITABLESDIR", FU_PATH_KIND_ACPI_TABLES},
	    {"FWUPD_FIRMWARESEARCH", FU_PATH_KIND_FIRMWARE_SEARCH},
	    {"FWUPD_SYSCONFDIR", FU_PATH_KIND_SYSCONFDIR},
	    {"FWUPD_LIBEXECDIR", FU_PATH_KIND_LIBEXECDIR},
	    {"FWUPD_LIBDIR_PKG", FU_PATH_KIND_LIBDIR_PKG},
	    {"FWUPD_DATADIR", FU_PATH_KIND_DATADIR_PKG},
	    {"FWUPD_LIBEXECDIR_PKG", FU_PATH_KIND_LIBEXECDIR_PKG},
	    {"FWUPD_DATADIR_VENDOR_IDS", FU_PATH_KIND_DATADIR_VENDOR_IDS},
	    {"FWUPD_DATADIR_QUIRKS", FU_PATH_KIND_DATADIR_QUIRKS},
	    {"FWUPD_EFIAPPDIR", FU_PATH_KIND_EFIAPPDIR},
	    {"CONFIGURATION_DIRECTORY", FU_PATH_KIND_SYSCONFDIR_PKG},
	    {"STATE_DIRECTORY", FU_PATH_KIND_LOCALSTATEDIR_PKG},
	    {"FWUPD_LOCALSTATEDIR_QUIRKS", FU_PATH_KIND_LOCALSTATEDIR_QUIRKS},
	    {"FWUPD_LOCALSTATEDIR_METADATA", FU_PATH_KIND_LOCALSTATEDIR_METADATA},
	    {"FWUPD_LOCALSTATEDIR_REMOTES", FU_PATH_KIND_LOCALSTATEDIR_REMOTES},
	    {"CACHE_DIRECTORY", FU_PATH_KIND_CACHEDIR_PKG},
	    {"LOCALCONF_DIRECTORY", FU_PATH_KIND_LOCALCONFDIR_PKG},
	    {"FWUPD_RUNDIR", FU_PATH_KIND_RUNDIR},
	    {"FWUPD_LOCKDIR", FU_PATH_KIND_LOCKDIR},
	    {"FWUPD_SYSFSFWATTRIBDIR", FU_PATH_KIND_SYSFSDIR_FW_ATTRIB},
	    {"FWUPD_HOSTFS_ROOT", FU_PATH_KIND_HOSTFS_ROOT},
	    {"FWUPD_HOSTFS_BOOT", FU_PATH_KIND_HOSTFS_BOOT},
	    {"FWUPD_DEVFS", FU_PATH_KIND_DEVFS},
	    {"FWUPD_LOCALTIME", FU_PATH_KIND_LOCALTIME},
	    {"FWUPD_DEBUGFSDIR", FU_PATH_KIND_DEBUGFSDIR},
	};

	g_return_if_fail(FU_IS_PATH_CONTEXT(self));

	for (guint i = 0; i < G_N_ELEMENTS(envmap); i++) {
		tmp = g_getenv(envmap[i].env);
		if (tmp != NULL) {
			g_debug("%s overrode %s -> %s",
				envmap[i].env,
				fu_path_kind_to_string(envmap[i].kind),
				tmp);
			fu_path_context_set_dir(self, envmap[i].kind, tmp);
		}
	}

#ifdef _WIN32
	/* WIN32 special case */
	tmp = g_getenv("USERPROFILE");
	if (tmp != NULL) {
		fu_path_context_build_dir(self,
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
		fu_path_context_add_prefix(self, FU_PATH_KIND_LOCALSTATEDIR, tmp);
		fu_path_context_add_prefix(self, FU_PATH_KIND_LOCALSTATEDIR_QUIRKS, tmp);
		fu_path_context_add_prefix(self, FU_PATH_KIND_LOCALSTATEDIR_METADATA, tmp);
		fu_path_context_add_prefix(self, FU_PATH_KIND_LOCALSTATEDIR_REMOTES, tmp);
	}

	/* snap usual case */
	tmp = g_getenv("SNAP");
	if (tmp != NULL) {
		fu_path_context_add_prefix(self, FU_PATH_KIND_SYSCONFDIR, tmp);
		fu_path_context_add_prefix(self, FU_PATH_KIND_LIBEXECDIR, tmp);
		fu_path_context_add_prefix(self, FU_PATH_KIND_LIBDIR_PKG, tmp);
		fu_path_context_add_prefix(self, FU_PATH_KIND_DATADIR_PKG, tmp);
		fu_path_context_add_prefix(self, FU_PATH_KIND_LIBEXECDIR_PKG, tmp);
		fu_path_context_add_prefix(self, FU_PATH_KIND_DATADIR_VENDOR_IDS, tmp);
		fu_path_context_add_prefix(self, FU_PATH_KIND_DATADIR_QUIRKS, tmp);
		fu_path_context_add_prefix(self, FU_PATH_KIND_EFIAPPDIR, tmp);
	}
}

static void
fu_path_context_finalize(GObject *object)
{
	FuPathContext *self = FU_PATH_CONTEXT(object);
	for (guint i = 0; i < FU_PATH_KIND_LAST; i++)
		g_free(self->dirnames[i]);
	G_OBJECT_CLASS(fu_path_context_parent_class)->finalize(object);
}

static void
fu_path_context_class_init(FuPathContextClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_path_context_finalize;
}

static void
fu_path_context_init(FuPathContext *self)
{
}

/**
 * fu_path_context_new:
 *
 * Returns: (transfer full): a #FuPathContext
 *
 * Since: 2.1.1
 **/
FuPathContext *
fu_path_context_new(void)
{
	return g_object_new(FU_TYPE_PATH_CONTEXT, NULL);
}

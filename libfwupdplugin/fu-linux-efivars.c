/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2015 Peter Jones <pjones@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuLinuxEfivars"

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>
#include <linux/fs.h>
#include <string.h>
#include <sys/ioctl.h>

#include "fwupd-error.h"

#include "fu-common.h"
#include "fu-linux-efivars.h"
#include "fu-path.h"

struct _FuLinuxEfivars {
	FuEfivars parent_instance;
};

G_DEFINE_TYPE(FuLinuxEfivars, fu_linux_efivars, FU_TYPE_EFIVARS)

static gchar *
fu_linux_efivars_get_path(void)
{
	g_autofree gchar *sysfsfwdir = fu_path_from_kind(FU_PATH_KIND_SYSFSDIR_FW);
	return g_build_filename(sysfsfwdir, "efi", "efivars", NULL);
}

static gchar *
fu_linux_efivars_get_filename(const gchar *guid, const gchar *name)
{
	g_autofree gchar *efivarsdir = fu_linux_efivars_get_path();
	return g_strdup_printf("%s/%s-%s", efivarsdir, name, guid);
}

static gboolean
fu_linux_efivars_supported(FuEfivars *efivars, GError **error)
{
	g_autofree gchar *efivarsdir = fu_linux_efivars_get_path();
	if (!g_file_test(efivarsdir, G_FILE_TEST_IS_DIR)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "kernel efivars support missing: %s",
			    efivarsdir);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_linux_efivars_set_immutable_fd(int fd, gboolean value, gboolean *value_old, GError **error)
{
	guint flags;
	gboolean is_immutable;
	int rc;

	/* get existing status */
	rc = ioctl(fd, FS_IOC_GETFLAGS, &flags); /* nocheck:blocked */
	if (rc < 0) {
		/* check for tmpfs */
		if (errno == ENOTTY || errno == ENOSYS) {
			is_immutable = FALSE;
		} else {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to get flags: %s",
				    g_strerror(errno));
			return FALSE;
		}
	} else {
		is_immutable = (flags & FS_IMMUTABLE_FL) > 0;
	}

	/* save the old value */
	if (value_old != NULL)
		*value_old = is_immutable;

	/* is this already correct */
	if (value) {
		if (is_immutable)
			return TRUE;
		flags |= FS_IMMUTABLE_FL;
	} else {
		if (!is_immutable)
			return TRUE;
		flags &= ~FS_IMMUTABLE_FL;
	}

	/* set the new status */
	rc = ioctl(fd, FS_IOC_SETFLAGS, &flags); /* nocheck:blocked */
	if (rc < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "failed to set flags: %s",
			    g_strerror(errno));
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_linux_efivars_set_immutable(const gchar *fn, gboolean value, gboolean *value_old, GError **error)
{
	gint fd;
	g_autoptr(GInputStream) istr = NULL;

	/* open file readonly */
	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "failed to open: %s",
			    g_strerror(errno));
		return FALSE;
	}
	istr = g_unix_input_stream_new(fd, TRUE);
	if (istr == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "failed to create stream");
		return FALSE;
	}
	return fu_linux_efivars_set_immutable_fd(fd, value, value_old, error);
}

static gboolean
fu_linux_efivars_delete(FuEfivars *efivars, const gchar *guid, const gchar *name, GError **error)
{
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;

	fn = fu_linux_efivars_get_filename(guid, name);
	file = g_file_new_for_path(fn);
	if (!g_file_query_exists(file, NULL)) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no key to delete");
		return FALSE;
	}
	if (!fu_linux_efivars_set_immutable(fn, FALSE, NULL, error)) {
		g_prefix_error(error, "failed to set %s as mutable: ", fn);
		return FALSE;
	}
	return g_file_delete(file, NULL, error);
}

static gboolean
fu_linux_efivars_delete_with_glob(FuEfivars *efivars,
				  const gchar *guid,
				  const gchar *name_glob,
				  GError **error)
{
	const gchar *fn;
	g_autofree gchar *nameguid_glob = NULL;
	g_autofree gchar *efivarsdir = fu_linux_efivars_get_path();
	g_autoptr(GDir) dir = NULL;

	dir = g_dir_open(efivarsdir, 0, error);
	if (dir == NULL)
		return FALSE;
	nameguid_glob = g_strdup_printf("%s-%s", name_glob, guid);
	while ((fn = g_dir_read_name(dir)) != NULL) {
		if (g_pattern_match_simple(nameguid_glob, fn)) {
			g_autofree gchar *keyfn = g_build_filename(efivarsdir, fn, NULL);
			g_autoptr(GFile) file = g_file_new_for_path(keyfn);
			if (!fu_linux_efivars_set_immutable(keyfn, FALSE, NULL, error)) {
				g_prefix_error(error, "failed to set %s as mutable: ", keyfn);
				return FALSE;
			}
			if (!g_file_delete(file, NULL, error))
				return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_linux_efivars_exists_guid(const gchar *guid)
{
	const gchar *fn;
	g_autofree gchar *efivarsdir = fu_linux_efivars_get_path();
	g_autoptr(GDir) dir = NULL;

	dir = g_dir_open(efivarsdir, 0, NULL);
	if (dir == NULL)
		return FALSE;
	while ((fn = g_dir_read_name(dir)) != NULL) {
		if (g_str_has_suffix(fn, guid))
			return TRUE;
	}
	return TRUE;
}

static gboolean
fu_linux_efivars_exists(FuEfivars *efivars, const gchar *guid, const gchar *name)
{
	g_autofree gchar *fn = NULL;

	/* any name */
	if (name == NULL)
		return fu_linux_efivars_exists_guid(guid);

	fn = fu_linux_efivars_get_filename(guid, name);
	return g_file_test(fn, G_FILE_TEST_EXISTS);
}

static gboolean
fu_linux_efivars_get_data(FuEfivars *efivars,
			  const gchar *guid,
			  const gchar *name,
			  guint8 **data,
			  gsize *data_sz,
			  guint32 *attr,
			  GError **error)
{
	gssize attr_sz;
	gssize data_sz_tmp;
	guint32 attr_tmp;
	guint64 sz;
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;
	g_autoptr(GInputStream) istr = NULL;

	/* open file as stream */
	fn = fu_linux_efivars_get_filename(guid, name);
	file = g_file_new_for_path(fn);
	istr = G_INPUT_STREAM(g_file_read(file, NULL, error));
	if (istr == NULL) {
		fwupd_error_convert(error);
		return FALSE;
	}
	info = g_file_input_stream_query_info(G_FILE_INPUT_STREAM(istr),
					      G_FILE_ATTRIBUTE_STANDARD_SIZE,
					      NULL,
					      error);
	if (info == NULL) {
		g_prefix_error(error, "failed to get stream info: ");
		fwupd_error_convert(error);
		return FALSE;
	}

	/* get total stream size */
	sz = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_STANDARD_SIZE);
	if (sz < 4) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "efivars file too small: %" G_GUINT64_FORMAT,
			    sz);
		return FALSE;
	}

	/* read out the attributes */
	attr_sz = g_input_stream_read(istr, &attr_tmp, sizeof(attr_tmp), NULL, error);
	if (attr_sz == -1) {
		g_prefix_error(error, "failed to read attr: ");
		fwupd_error_convert(error);
		return FALSE;
	}
	if (attr != NULL)
		*attr = attr_tmp;

	/* read out the data */
	data_sz_tmp = sz - sizeof(attr_tmp);
	if (data_sz_tmp == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "no data to read");
		return FALSE;
	}
	if (data_sz != NULL)
		*data_sz = data_sz_tmp;
	if (data != NULL) {
		g_autofree guint8 *data_tmp = g_malloc0(data_sz_tmp);
		if (!g_input_stream_read_all(istr, data_tmp, data_sz_tmp, NULL, NULL, error)) {
			g_prefix_error(error, "failed to read data: ");
			return FALSE;
		}
		*data = g_steal_pointer(&data_tmp);
	}
	return TRUE;
}

static GPtrArray *
fu_linux_efivars_get_names(FuEfivars *efivars, const gchar *guid, GError **error)
{
	const gchar *name_guid;
	g_autofree gchar *path = fu_linux_efivars_get_path();
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GPtrArray) names = g_ptr_array_new_with_free_func(g_free);

	/* find names with matching GUID */
	dir = g_dir_open(path, 0, error);
	if (dir == NULL)
		return NULL;
	while ((name_guid = g_dir_read_name(dir)) != NULL) {
		gsize name_guidsz = strlen(name_guid);
		if (name_guidsz < 38)
			continue;
		if (g_strcmp0(name_guid + name_guidsz - 36, guid) == 0) {
			g_ptr_array_add(names, g_strndup(name_guid, name_guidsz - 37));
		}
	}

	/* nothing found */
	if (names->len == 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "no names for GUID %s",
			    guid);
		return NULL;
	}

	/* success */
	return g_steal_pointer(&names);
}

static GFileMonitor *
fu_linux_efivars_get_monitor(FuEfivars *efivars,
			     const gchar *guid,
			     const gchar *name,
			     GError **error)
{
	g_autofree gchar *fn = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileMonitor) monitor = NULL;

	fn = fu_linux_efivars_get_filename(guid, name);
	file = g_file_new_for_path(fn);
	monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, error);
	if (monitor == NULL) {
		fwupd_error_convert(error);
		return NULL;
	}
	g_file_monitor_set_rate_limit(monitor, 5000);
	return g_steal_pointer(&monitor);
}

static guint64
fu_linux_efivars_space_used(FuEfivars *efivars, GError **error)
{
	const gchar *fn;
	guint64 total = 0;
	g_autoptr(GDir) dir = NULL;
	g_autofree gchar *path = fu_linux_efivars_get_path();
	g_autoptr(GFile) file_fs = g_file_new_for_path(path);
	g_autoptr(GFileInfo) info_fs = NULL;
	g_autoptr(GError) error_local = NULL;

	/* this is only supported in new kernels */
	info_fs = g_file_query_info(file_fs,
				    G_FILE_ATTRIBUTE_FILESYSTEM_USED,
				    G_FILE_QUERY_INFO_NONE,
				    NULL,
				    &error_local);
	if (info_fs == NULL) {
		g_debug("failed to get efivars used space: %s", error_local->message);
	} else {
		total = g_file_info_get_attribute_uint64(info_fs, G_FILE_ATTRIBUTE_FILESYSTEM_USED);
		if (total > 0)
			return total;
	}

	/* stat each file */
	dir = g_dir_open(path, 0, error);
	if (dir == NULL)
		return G_MAXUINT64;
	while ((fn = g_dir_read_name(dir)) != NULL) {
		guint64 sz;
		g_autofree gchar *pathfn = g_build_filename(path, fn, NULL);
		g_autoptr(GFile) file = g_file_new_for_path(pathfn);
		g_autoptr(GFileInfo) info = NULL;

		info = g_file_query_info(file,
					 G_FILE_ATTRIBUTE_STANDARD_ALLOCATED_SIZE
					 "," G_FILE_ATTRIBUTE_STANDARD_SIZE,
					 G_FILE_QUERY_INFO_NONE,
					 NULL,
					 error);
		if (info == NULL) {
			fwupd_error_convert(error);
			return G_MAXUINT64;
		}
		sz = g_file_info_get_attribute_uint64(info,
						      G_FILE_ATTRIBUTE_STANDARD_ALLOCATED_SIZE);
		if (sz == 0)
			sz = g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_STANDARD_SIZE);
		total += sz;
	}

	/* success */
	return total;
}

static gboolean
fu_linux_efivars_set_data(FuEfivars *efivars,
			  const gchar *guid,
			  const gchar *name,
			  const guint8 *data,
			  gsize sz,
			  guint32 attr,
			  GError **error)
{
	int fd;
	int open_wflags;
	gboolean was_immutable;
	g_autofree gchar *fn = fu_linux_efivars_get_filename(guid, name);
	g_autofree guint8 *buf = g_malloc0(sizeof(guint32) + sz);
	g_autoptr(GFile) file = g_file_new_for_path(fn);
	g_autoptr(GOutputStream) ostr = NULL;

	/* create empty file so we can clear the immutable bit before writing */
	if (!g_file_query_exists(file, NULL)) {
		g_autoptr(GFileOutputStream) ostr_tmp = NULL;
		ostr_tmp = g_file_create(file, G_FILE_CREATE_NONE, NULL, error);
		if (ostr_tmp == NULL) {
			fwupd_error_convert(error);
			return FALSE;
		}
		if (!g_output_stream_close(G_OUTPUT_STREAM(ostr_tmp), NULL, error)) {
			g_prefix_error(error, "failed to touch efivarsfs: ");
			fwupd_error_convert(error);
			return FALSE;
		}
	}
	if (!fu_linux_efivars_set_immutable(fn, FALSE, &was_immutable, error)) {
		g_prefix_error(error, "failed to set %s as mutable: ", fn);
		return FALSE;
	}

	/* open file for writing, optionally append */
	open_wflags = O_WRONLY;
	if (attr & FU_EFIVARS_ATTR_APPEND_WRITE)
		open_wflags |= O_APPEND;
	fd = open(fn, open_wflags);
	if (fd < 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "failed to open %s: %s",
			    fn,
			    g_strerror(errno));
		return FALSE;
	}
	ostr = g_unix_output_stream_new(fd, TRUE);
	memcpy(buf, &attr, sizeof(attr));     /* nocheck:blocked */
	memcpy(buf + sizeof(attr), data, sz); /* nocheck:blocked */
	if (g_output_stream_write(ostr, buf, sizeof(attr) + sz, NULL, error) < 0) {
		g_prefix_error(error, "failed to write data to efivarsfs: ");
		fwupd_error_convert(error);
		return FALSE;
	}

	/* set as immutable again */
	if (was_immutable && !fu_linux_efivars_set_immutable(fn, TRUE, NULL, error)) {
		g_prefix_error(error, "failed to set %s as immutable: ", fn);
		fwupd_error_convert(error);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_linux_efivars_init(FuLinuxEfivars *self)
{
}

static void
fu_linux_efivars_class_init(FuLinuxEfivarsClass *klass)
{
	FuEfivarsClass *efivars_class = FU_EFIVARS_CLASS(klass);
	efivars_class->supported = fu_linux_efivars_supported;
	efivars_class->space_used = fu_linux_efivars_space_used;
	efivars_class->exists = fu_linux_efivars_exists;
	efivars_class->get_monitor = fu_linux_efivars_get_monitor;
	efivars_class->get_data = fu_linux_efivars_get_data;
	efivars_class->set_data = fu_linux_efivars_set_data;
	efivars_class->delete = fu_linux_efivars_delete;
	efivars_class->delete_with_glob = fu_linux_efivars_delete_with_glob;
	efivars_class->get_names = fu_linux_efivars_get_names;
}

FuEfivars *
fu_efivars_new(void)
{
	return FU_EFIVARS(g_object_new(FU_TYPE_LINUX_EFIVARS, NULL));
}

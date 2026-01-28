/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include <errno.h>
#include <glib/gstdio.h>

#ifdef _WIN32
#include <stdlib.h>
#endif

#include "fwupd-error.h"

#include "fu-common.h"
#include "fu-path.h"

/**
 * fu_path_rmtree:
 * @directory: a directory name
 * @error: (nullable): optional return location for an error
 *
 * Recursively removes a directory.
 *
 * Returns: %TRUE for success, %FALSE otherwise
 *
 * Since: 1.8.2
 **/
gboolean
fu_path_rmtree(const gchar *directory, GError **error)
{
	const gchar *filename;
	g_autoptr(GDir) dir = NULL;

	g_return_val_if_fail(directory != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* try to open */
	g_debug("removing %s", directory);
	dir = g_dir_open(directory, 0, error);
	if (dir == NULL)
		return FALSE;

	/* find each */
	while ((filename = g_dir_read_name(dir))) {
		g_autofree gchar *src = NULL;
		src = g_build_filename(directory, filename, NULL);
		if (g_file_test(src, G_FILE_TEST_IS_DIR)) {
			if (!fu_path_rmtree(src, error))
				return FALSE;
		} else {
			if (g_unlink(src) != 0) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "Failed to delete: %s",
					    src);
				return FALSE;
			}
		}
	}
	if (g_remove(directory) != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to delete: %s",
			    directory);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_path_get_file_list_internal(GPtrArray *files, const gchar *directory, GError **error)
{
	const gchar *filename;
	g_autoptr(GDir) dir = NULL;

	/* try to open */
	dir = g_dir_open(directory, 0, error);
	if (dir == NULL) {
		fwupd_error_convert(error);
		return FALSE;
	}

	/* find each */
	while ((filename = g_dir_read_name(dir))) {
		g_autofree gchar *src = g_build_filename(directory, filename, NULL);
		if (g_file_test(src, G_FILE_TEST_IS_SYMLINK))
			continue;
		if (g_file_test(src, G_FILE_TEST_IS_DIR)) {
			if (!fu_path_get_file_list_internal(files, src, error))
				return FALSE;
		} else {
			g_ptr_array_add(files, g_steal_pointer(&src));
		}
	}
	return TRUE;
}

/**
 * fu_path_get_files:
 * @path: a directory name
 * @error: (nullable): optional return location for an error
 *
 * Returns every file found under @directory, and any subdirectory.
 * If any path under @directory cannot be accessed due to permissions an error
 * will be returned.
 *
 * Returns: (transfer container) (element-type utf8): array of files, or %NULL for error
 *
 * Since: 1.8.2
 **/
GPtrArray *
fu_path_get_files(const gchar *path, GError **error)
{
	g_autoptr(GPtrArray) files = g_ptr_array_new_with_free_func(g_free);

	g_return_val_if_fail(path != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	if (!fu_path_get_file_list_internal(files, path, error))
		return NULL;
	return g_steal_pointer(&files);
}

/**
 * fu_path_mkdir:
 * @dirname: a directory name
 * @error: (nullable): optional return location for an error
 *
 * Creates any required directories, including any parent directories.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.2
 **/
gboolean
fu_path_mkdir(const gchar *dirname, GError **error)
{
	g_return_val_if_fail(dirname != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!g_file_test(dirname, G_FILE_TEST_IS_DIR))
		g_debug("creating path %s", dirname);
	if (g_mkdir_with_parents(dirname, 0755) == -1) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "Failed to create '%s': %s",
			    dirname,
			    fwupd_strerror(errno));
		return FALSE;
	}
	return TRUE;
}

/**
 * fu_path_mkdir_parent:
 * @filename: a full pathname
 * @error: (nullable): optional return location for an error
 *
 * Creates any required directories, including any parent directories.
 *
 * Returns: %TRUE for success
 *
 * Since: 1.8.2
 **/
gboolean
fu_path_mkdir_parent(const gchar *filename, GError **error)
{
	g_autofree gchar *parent = NULL;

	g_return_val_if_fail(filename != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	parent = g_path_get_dirname(filename);
	return fu_path_mkdir(parent, error);
}

/**
 * fu_path_find_program:
 * @basename: the program to search
 * @error: (nullable): optional return location for an error
 *
 * Looks for a program in the PATH variable
 *
 * Returns: a new #gchar, or %NULL for error
 *
 * Since: 1.8.2
 **/
gchar *
fu_path_find_program(const gchar *basename, GError **error)
{
	gchar *fn = g_find_program_in_path(basename);
	if (fn == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "missing executable %s in PATH",
			    basename);
		return NULL;
	}
	return fn;
}

static gint
fu_path_glob_sort_cb(gconstpointer a, gconstpointer b)
{
	return g_strcmp0(*(const gchar **)a, *(const gchar **)b);
}

/**
 * fu_path_glob:
 * @directory: a directory path
 * @pattern: a glob pattern, e.g. `*foo*`
 * @error: (nullable): optional return location for an error
 *
 * Returns all the filenames that match a specific glob pattern.
 * Any results are sorted. No matching files will set @error.
 *
 * Returns:  (element-type utf8) (transfer container): matching files, or %NULL
 *
 * Since: 1.8.2
 **/
GPtrArray *
fu_path_glob(const gchar *directory, const gchar *pattern, GError **error)
{
	const gchar *basename;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GPtrArray) files = g_ptr_array_new_with_free_func(g_free);

	g_return_val_if_fail(directory != NULL, NULL);
	g_return_val_if_fail(pattern != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	dir = g_dir_open(directory, 0, error);
	if (dir == NULL)
		return NULL;
	while ((basename = g_dir_read_name(dir)) != NULL) {
		if (!g_pattern_match_simple(pattern, basename))
			continue;
		g_ptr_array_add(files, g_build_filename(directory, basename, NULL));
	}
	if (files->len == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_FOUND,
				    "no files matched pattern");
		return NULL;
	}
	g_ptr_array_sort(files, fu_path_glob_sort_cb);
	return g_steal_pointer(&files);
}

/**
 * fu_path_make_absolute:
 * @filename: a path to a filename, perhaps symlinked
 * @error: (nullable): optional return location for an error
 *
 * Returns the resolved absolute file name.
 *
 * Returns: (transfer full): path, or %NULL on error
 *
 * Since: 2.0.0
 **/
gchar *
fu_path_make_absolute(const gchar *filename, GError **error)
{
	char full_tmp[PATH_MAX];

	g_return_val_if_fail(filename != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

#ifdef HAVE_REALPATH
	if (realpath(filename, full_tmp) == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "cannot resolve path: %s",
			    fwupd_strerror(errno));
		return NULL;
	}
#else
	if (_fullpath(full_tmp, filename, sizeof(full_tmp)) == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "cannot resolve path: %s",
			    fwupd_strerror(errno));
		return NULL;
	}
#endif
	if (!g_file_test(full_tmp, G_FILE_TEST_EXISTS)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "cannot find path: %s",
			    full_tmp);
		return NULL;
	}
	return g_strdup(full_tmp);
}

/**
 * fu_path_get_symlink_target:
 * @filename: a path to a symlink
 * @error: (nullable): optional return location for an error
 *
 * Returns the symlink target.
 *
 * Returns: (transfer full): path, or %NULL on error
 *
 * Since: 2.0.0
 **/
gchar *
fu_path_get_symlink_target(const gchar *filename, GError **error)
{
	const gchar *target;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GFileInfo) info = NULL;

	file = g_file_new_for_path(filename);
	info = g_file_query_info(file,
				 G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET,
				 G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
				 NULL,
				 error);
	if (info == NULL) {
		fwupd_error_convert(error);
		return NULL;
	}
	target =
	    g_file_info_get_attribute_byte_string(info, G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET);
	if (target == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND, "no symlink target");
		return NULL;
	}

	/* success */
	return g_strdup(target);
}

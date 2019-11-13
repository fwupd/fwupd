/*
 * Copyright (C) 2019 9elements Agency GmbH <patrick.rudolph@9elements.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>
#include <stdio.h>

#include "fu-plugin-coreboot.h"

#define SYSFS_BUS "/sys/bus/coreboot/"

/* Tries to detect the 'coreboot' kernel module presence. */
gboolean
fu_plugin_coreboot_sysfs_probe (void)
{
	return g_file_test(SYSFS_BUS, G_FILE_TEST_EXISTS|
				      G_FILE_TEST_IS_DIR);
}

/* iterates over sysfs directories until the given ID is found. */
static gchar *
fu_plugin_coreboot_find_sysfs (const guint id,
			       const gchar *base_path,
			       const gchar *extension_path,
			       GError **error)
{
	GDir *dir;
	const gchar *subdir = NULL;
	gchar *tmp = NULL;
	gboolean ret;

	dir = g_dir_open (base_path, 0, error);
	if (!dir)
		return NULL;

	while (1) {
		gchar *fp;
		guint64 i;

		subdir = g_dir_read_name(dir);
		if (!subdir)
			break;

		fp = g_strdup_printf ("%s/%s/%s/id", base_path, subdir,
				      extension_path);

		ret = g_file_get_contents (fp, &tmp, NULL, NULL);
		g_free (fp);

		if (!ret) {
			continue;
		}
		i = g_ascii_strtoull (tmp, NULL, 16);
		g_free (tmp);

		if (id == i)
			break;
	};

	g_dir_close (dir);

	if (subdir == NULL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_ARGUMENT,
			     "Requested id not found");
		return NULL;
	}
	return g_strdup_printf ("%s/%s/%s/", base_path, subdir, extension_path);
}

/* Returns the coreboot tables with given tag */
gchar *
fu_plugin_coreboot_find_cb_table (const guint tag, gsize *length,
				  GError **error)
{
	gchar *fp;
	gboolean ret;
	gchar *path;
	gchar *tmp;

	path = fu_plugin_coreboot_find_sysfs (tag, SYSFS_BUS "/devices/",
					      "attributes", error);
	if (!path)
		return NULL;

	fp = g_strdup_printf ("%s/data", path);
	g_free (path);

	ret = g_file_get_contents (fp, &tmp, length, error);
	g_free (fp);

	if (!ret)
		return NULL;

	return tmp;
}

/* Returns the CBMEM buffer with given id */
gchar *
fu_plugin_coreboot_find_cbmem (const guint id, gsize *length, goffset *address,
			       GError **error)
{
	gchar *fp;
	gboolean ret;
	gchar *tmp;
	gchar *path;

	path = fu_plugin_coreboot_find_sysfs (id, SYSFS_BUS "/drivers/cbmem/",
					      "cbmem_attributes", error);
	if (!path)
		return NULL;

	if (address != NULL) {
		/* Store the physical CBMEM buffer address if requested */

		fp = g_strdup_printf ("%s/address", path);

		ret = g_file_get_contents (fp, &tmp, NULL, error);
		g_free (fp);

		if (!ret) {
			g_free (path);
			return NULL;
		}

		*address = (goffset) g_ascii_strtoull (tmp, NULL, 16);
		g_free (tmp);
	}

	fp = g_strdup_printf ("%s/data", path);
	g_free (path);

	ret = g_file_get_contents (fp, &tmp, length, error);
	g_free (fp);

	if (!ret)
		return NULL;

	return tmp;
}

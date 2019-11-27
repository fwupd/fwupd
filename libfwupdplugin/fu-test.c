/*
 * Copyright (C) 2010-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gio.h>

#include <limits.h>
#include <stdlib.h>

#include "fu-test.h"
#include "fu-common.h"


/**
 * fu_test_get_filename:
 * @testdatadirs: semicolon delimitted list of directories
 * @filename: the filename to look for
 *
 * Returns the first path that matches filename in testdatadirs
 *
 * Returns: (transfer full): full path to file or NULL
 *
 * Since: 0.9.1
 **/
gchar *
fu_test_get_filename (const gchar *testdatadirs, const gchar *filename)
{
	g_auto(GStrv) split = g_strsplit (testdatadirs, ":", -1);
	for (guint i = 0; split[i] != NULL; i++) {
		g_autofree gchar *tmp = NULL;
		g_autofree gchar *path = NULL;
		path = g_build_filename (split[i], filename, NULL);
		tmp = fu_common_realpath (path, NULL);
		if (tmp != NULL)
			return g_steal_pointer (&tmp);
	}
	return NULL;
}

/**
 * fu_test_compare_lines:
 * @txt1: First line to compare
 * @txt2: second line to compare
 * @error: A #GError or #NULL
 *
 * Compare two lines.
 *
 * Returns: #TRUE if identical, #FALSE if not (diff is set in error)
 *
 * Since: 1.0.4
 **/
gboolean
fu_test_compare_lines (const gchar *txt1, const gchar *txt2, GError **error)
{
	g_autofree gchar *output = NULL;

	/* exactly the same */
	if (g_strcmp0 (txt1, txt2) == 0)
		return TRUE;

	/* matches a pattern */
	if (fu_common_fnmatch (txt2, txt1))
		return TRUE;

	/* save temp files and diff them */
	if (!g_file_set_contents ("/tmp/a", txt1, -1, error))
		return FALSE;
	if (!g_file_set_contents ("/tmp/b", txt2, -1, error))
		return FALSE;
	if (!g_spawn_command_line_sync ("diff -urNp /tmp/b /tmp/a",
					&output, NULL, NULL, error))
		return FALSE;

	/* just output the diff */
	g_set_error_literal (error, 1, 0, output);
	return FALSE;
}

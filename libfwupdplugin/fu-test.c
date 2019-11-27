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

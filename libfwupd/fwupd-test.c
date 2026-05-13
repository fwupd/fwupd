/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd-test.h"

gboolean
fu_test_compare_lines(const gchar *txt1, const gchar *txt2, GError **error)
{
	g_autofree gchar *output = NULL;

	/* exactly the same */
	if (g_strcmp0(txt1, txt2) == 0)
		return TRUE;

	/* matches a pattern */
	if (g_pattern_match_simple(txt2, txt1))
		return TRUE;

	/* save temp files and diff them */
	if (!g_file_set_contents("/tmp/a", txt1, -1, error))
		return FALSE;
	if (!g_file_set_contents("/tmp/b", txt2, -1, error))
		return FALSE;
	if (!g_spawn_command_line_sync("diff -urNp /tmp/b /tmp/a", &output, NULL, NULL, error))
		return FALSE;

	/* just output the diff */
	g_set_error_literal(error, 1, 0, output);
	return FALSE;
}

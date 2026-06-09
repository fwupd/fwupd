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
	g_autoptr(GString) txt1safe = g_string_new(txt1);
	g_autoptr(GString) txt2safe = g_string_new(txt2);

	/* convert the new non-breaking space back into a normal space:
	 * https://gitlab.gnome.org/GNOME/glib/commit/76af5dabb4a25956a6c41a75c0c7feeee74496da */
	g_string_replace(txt1safe, " ", " ", 0);
	g_string_replace(txt2safe, " ", " ", 0);

	/* exactly the same */
	if (g_strcmp0(txt1safe->str, txt2safe->str) == 0)
		return TRUE;

	/* matches a pattern */
	if (g_pattern_match_simple(txt2safe->str, txt1safe->str))
		return TRUE;

	/* save temp files and diff them */
	if (!g_file_set_contents("/tmp/a", txt1safe->str, txt1safe->len, error))
		return FALSE;
	if (!g_file_set_contents("/tmp/b", txt2safe->str, txt2safe->len, error))
		return FALSE;
	if (!g_spawn_command_line_sync("diff -urNp /tmp/b /tmp/a", &output, NULL, NULL, error))
		return FALSE;

	/* just output the diff */
	g_set_error_literal(error, 1, 0, output);
	return FALSE;
}

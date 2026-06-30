/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>
#include <glib/gstdio.h>

#define G_FILE_SET_CONTENTS_NONE       0
#define G_FILE_SET_CONTENTS_CONSISTENT (1 << 0)

static inline gboolean
g_file_set_contents_full(const gchar *filename,
			 const gchar *contents,
			 gssize length,
			 guint flags,
			 int mode,
			 GError **error)
{
	/* flags are ignored -- there is not much more we can do using this old API */
	if (!g_file_set_contents(filename, contents, length, error))
		return FALSE;
	/* failure leaves the file with the wrong permission -- nothing we can do */
	if (g_chmod(filename, mode) != 0) {
		/* nocheck:error */
		g_set_error_literal(error, G_FILE_ERROR, G_FILE_ERROR_PERM, "failed to chmod");
		return FALSE;
	}
	return TRUE;
}

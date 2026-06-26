/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>

static inline gboolean
g_clear_fd(int *fd_ptr, GError **error)
{
	int fd = *fd_ptr;
	*fd_ptr = -1;
	if (fd < 0)
		return TRUE;
	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	return g_close(fd, error);
	G_GNUC_END_IGNORE_DEPRECATIONS
}

static inline void
_g_clear_fd_ignore_error(int *fd_ptr)
{
	int errsv = errno;
	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	if (!g_clear_fd(fd_ptr, NULL)) {
		/* do nothing: we ignore all errors, except for EBADF which
		 * is a programming error, checked for by g_close(). */
	}
	G_GNUC_END_IGNORE_DEPRECATIONS
	errno = errsv;
}

#define g_autofd _GLIB_CLEANUP(_g_clear_fd_ignore_error)

/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

/* see https://bugzilla.gnome.org/show_bug.cgi?id=113075 */
#ifndef G_GNUC_NON_NULL
#if !defined(SUPPORTED_BUILD) && !defined(_WIN32) && (__GNUC__ > 3) ||                             \
    (__GNUC__ == 3 && __GNUC_MINOR__ >= 3)
#define G_GNUC_NON_NULL(params...) __attribute__((nonnull(params)))
#else
#define G_GNUC_NON_NULL(params...)
#endif
#endif

#if !GLIB_CHECK_VERSION(2, 70, 0)
#define g_prefix_error_literal	  g_prefix_error
#define g_spawn_check_wait_status g_spawn_check_exit_status
#define g_source_set_static_name(n1, n2)
static inline int
g_steal_fd(int *fd_ptr)
{
	int fd = *fd_ptr;
	*fd_ptr = -1;
	return fd;
}
#endif
#if !GLIB_CHECK_VERSION(2, 72, 0)
#define g_log_get_debug_enabled() (g_getenv("FWUPD_VERBOSE") != NULL)
#endif

#if !GLIB_CHECK_VERSION(2, 76, 0)
static inline gboolean
g_clear_fd(int *fd_ptr, GError **error)
{
	int fd = *fd_ptr;

	*fd_ptr = -1;

	if (fd < 0)
		return TRUE;

	/* suppress "Not available before" warning */
	G_GNUC_BEGIN_IGNORE_DEPRECATIONS
	return g_close(fd, error);
	G_GNUC_END_IGNORE_DEPRECATIONS
}

/* Not public API */
static inline void
_g_clear_fd_ignore_error(int *fd_ptr)
{
	/* Don't overwrite thread-local errno if closing the fd fails */
	int errsv = errno;

	/* suppress "Not available before" warning */
	G_GNUC_BEGIN_IGNORE_DEPRECATIONS

	if (!g_clear_fd(fd_ptr, NULL)) {
		/* Do nothing: we ignore all errors, except for EBADF which
		 * is a programming error, checked for by g_close(). */
	}

	G_GNUC_END_IGNORE_DEPRECATIONS

	errno = errsv;
}

#define g_autofd _GLIB_CLEANUP(_g_clear_fd_ignore_error)
#endif

#if !GLIB_CHECK_VERSION(2, 80, 0)
#define g_task_return_new_error_literal g_task_return_new_error
#endif

#ifndef G_GNUC_FLAG_ENUM
#if g_macro__has_attribute(flag_enum)
#define G_GNUC_FLAG_ENUM __attribute__((flag_enum))
#else
#define G_GNUC_FLAG_ENUM
#endif
#endif

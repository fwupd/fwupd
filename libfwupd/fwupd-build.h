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
#endif

#if !GLIB_CHECK_VERSION(2, 80, 0)
#define g_task_return_new_error_literal g_task_return_new_error
#endif

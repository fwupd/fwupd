/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

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

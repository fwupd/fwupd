/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define g_log_get_debug_enabled() (g_getenv("FWUPD_VERBOSE") != NULL)

G_END_DECLS

/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

gboolean
fu_test_compare_lines(const gchar *txt1, const gchar *txt2, GError **error);

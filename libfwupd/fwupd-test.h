/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-build.h"

G_BEGIN_DECLS

gboolean
fu_test_compare_lines(const gchar *txt1, const gchar *txt2, GError **error);

G_END_DECLS

/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-path.h"

gboolean
fu_path_fnmatch_impl(const gchar *pattern, const gchar *str);

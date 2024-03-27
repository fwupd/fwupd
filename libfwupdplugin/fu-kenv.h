/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

gchar *
fu_kenv_get_string(const gchar *key, GError **error) G_GNUC_NON_NULL(1);

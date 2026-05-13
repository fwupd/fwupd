/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

gchar *
fu_release_uri_get_scheme(const gchar *uri) G_GNUC_NON_NULL(1);

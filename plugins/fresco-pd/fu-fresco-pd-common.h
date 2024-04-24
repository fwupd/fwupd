/*
 * Copyright 2020 Fresco Logic
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

gchar *
fu_fresco_pd_version_from_buf(const guint8 ver[4]);

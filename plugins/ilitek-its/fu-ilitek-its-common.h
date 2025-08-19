/*
 * Copyright 2025 Joe Hong <joe_hung@ilitek.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

guint16
fu_ilitek_its_get_crc(GBytes *blob, gsize count);
gchar *
fu_ilitek_its_convert_version(guint64 version_raw);

/*
 * Copyright 2025 Joe Hong <JoeHung@ilitek.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

guint16
fu_ilitek_its_get_crc(GBytes *blob, gsize count);

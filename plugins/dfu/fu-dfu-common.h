/*
 * Copyright 2015 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

/* helpers */
GBytes *
fu_dfu_utils_bytes_join_array(GPtrArray *chunks);

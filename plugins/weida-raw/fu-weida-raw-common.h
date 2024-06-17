/*
 * Copyright 2024 Randy Lai <randy.lai@weidahitech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>

gboolean
fu_weida_raw_block_is_empty(const guint8 *data, gsize datasz);

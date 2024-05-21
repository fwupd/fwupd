/*
 * Copyright 2024 Randy Lai <randy.lai@weidahitech.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib-object.h>


gboolean
fu_weida_raw_block_is_empty(const guint8 *data, gsize datasz);

guint16
fu_weida_raw_misr_16b(guint16 current_value, guint16 new_value);

guint16
fu_weida_raw_misr_for_bytes(guint16 current_value, guint8 *bytes, guint start, guint size);

guint16
fu_weida_raw_misr_for_halfwords(guint16 current_value, guint8 *buf, guint start, guint hword_count);

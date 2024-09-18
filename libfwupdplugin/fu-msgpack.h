/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#include "fu-msgpack-struct.h"

GPtrArray *
fu_msgpack_parse(GByteArray *buf, GError **error) G_GNUC_NON_NULL(1);
GByteArray *
fu_msgpack_write(GPtrArray *items, GError **error) G_GNUC_NON_NULL(1);
FuMsgpackItem *
fu_msgpack_map_lookup(GPtrArray *items, guint idx, const gchar *key, GError **error)
    G_GNUC_NON_NULL(1, 3);

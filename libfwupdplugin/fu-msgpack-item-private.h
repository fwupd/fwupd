/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-msgpack-item.h"

gboolean
fu_msgpack_item_append(FuMsgpackItem *self, GByteArray *buf, GError **error) G_GNUC_NON_NULL(1, 2);
FuMsgpackItem *
fu_msgpack_item_parse(GByteArray *buf, gsize *offset, GError **error) G_GNUC_NON_NULL(1, 2);

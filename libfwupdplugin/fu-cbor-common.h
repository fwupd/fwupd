/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-cbor-item.h"

FuCborItem *
fu_cbor_parse(GInputStream *stream,
	      gsize *offset,
	      guint max_depth,
	      guint max_items,
	      guint max_length,
	      GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);

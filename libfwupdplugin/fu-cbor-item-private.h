/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-cbor-item.h"

FuCborItem *
fu_cbor_item_new_string_steal(gchar *value) G_GNUC_WARN_UNUSED_RESULT;

/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-partial-input-stream.h"

gsize
fu_partial_input_stream_get_offset(FuPartialInputStream *self) G_GNUC_NON_NULL(1);
gsize
fu_partial_input_stream_get_size(FuPartialInputStream *self) G_GNUC_NON_NULL(1);

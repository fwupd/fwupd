/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-stream-input-stream.h"

void
fu_stream_input_stream_set_base_stream(FuStreamInputStream *self, GInputStream *base_stream)
    G_GNUC_NON_NULL(1, 2);

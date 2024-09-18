/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-partial-input-stream.h"

#define FU_TYPE_COMPOSITE_INPUT_STREAM (fu_composite_input_stream_get_type())

G_DECLARE_FINAL_TYPE(FuCompositeInputStream,
		     fu_composite_input_stream,
		     FU,
		     COMPOSITE_INPUT_STREAM,
		     GInputStream)

GInputStream *
fu_composite_input_stream_new(void);
void
fu_composite_input_stream_add_bytes(FuCompositeInputStream *self, GBytes *bytes)
    G_GNUC_NON_NULL(1, 2);
void
fu_composite_input_stream_add_partial_stream(FuCompositeInputStream *self,
					     FuPartialInputStream *partial_stream)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_composite_input_stream_add_stream(FuCompositeInputStream *self,
				     GInputStream *stream,
				     GError **error) G_GNUC_NON_NULL(1, 2);

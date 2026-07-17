/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-input-stream.h"

#define FU_TYPE_STREAM_INPUT_STREAM (fu_stream_input_stream_get_type())
G_DECLARE_DERIVABLE_TYPE(FuStreamInputStream,
			 fu_stream_input_stream,
			 FU,
			 STREAM_INPUT_STREAM,
			 FuInputStream)

struct _FuStreamInputStreamClass {
	FuInputStreamClass parent_class;
};

FuInputStream *
fu_stream_input_stream_from_stream(GInputStream *stream) G_GNUC_NON_NULL(1);

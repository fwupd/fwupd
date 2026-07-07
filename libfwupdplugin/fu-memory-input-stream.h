/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-input-stream.h"

#define FU_TYPE_MEMORY_INPUT_STREAM (fu_memory_input_stream_get_type())
G_DECLARE_FINAL_TYPE(FuMemoryInputStream,
		     fu_memory_input_stream,
		     FU,
		     MEMORY_INPUT_STREAM,
		     FuInputStream)

FuInputStream *
fu_memory_input_stream_new(void) G_GNUC_WARN_UNUSED_RESULT;
FuInputStream *
fu_memory_input_stream_new_from_bytes(GBytes *bytes) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
FuInputStream *
fu_memory_input_stream_new_from_data(const void *data,
				     gssize len,
				     GDestroyNotify destroy) G_GNUC_WARN_UNUSED_RESULT;

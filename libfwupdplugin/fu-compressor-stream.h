/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-compressor-struct.h"
#include "fu-stream-input-stream.h"

#define FU_TYPE_COMPRESSOR_STREAM (fu_compressor_stream_get_type())
G_DECLARE_FINAL_TYPE(FuCompressorStream,
		     fu_compressor_stream,
		     FU,
		     COMPRESSOR_STREAM,
		     FuStreamInputStream)

FuInputStream *
fu_compressor_stream_new_decompress(FuInputStream *source,
				    FuCompressorFormat format,
				    GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
FuInputStream *
fu_compressor_stream_new_compress(FuInputStream *source,
				  FuCompressorFormat format,
				  GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);

/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-stream-input-stream.h"

#define FU_TYPE_FILE_INPUT_STREAM (fu_file_input_stream_get_type())
G_DECLARE_FINAL_TYPE(FuFileInputStream,
		     fu_file_input_stream,
		     FU,
		     FILE_INPUT_STREAM,
		     FuStreamInputStream)

FuFileInputStream *
fu_file_input_stream_from_file(GFile *file,
			       GCancellable *cancellable,
			       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
GFileInfo *
fu_file_input_stream_query_info(FuFileInputStream *stream,
				const gchar *attributes,
				GCancellable *cancellable,
				GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);

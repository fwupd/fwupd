/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-input-stream.h"

typedef GFileInputStream FuFileInputStream;	      /* nocheck:blocked */
typedef GFileInputStreamClass FuFileInputStreamClass; /* nocheck:blocked */

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuFileInputStream, g_object_unref)   /* nocheck:blocked */
#define FU_FILE_INPUT_STREAM(o)	      G_FILE_INPUT_STREAM(o)	   /* nocheck:blocked */
#define FU_TYPE_FILE_INPUT_STREAM     G_TYPE_FILE_INPUT_STREAM	   /* nocheck:blocked */
#define FU_IS_FILE_INPUT_STREAM(o)    G_IS_FILE_INPUT_STREAM(o)	   /* nocheck:blocked */
#define FU_FILE_INPUT_STREAM_CLASS(o) G_FILE_INPUT_STREAM_CLASS(o) /* nocheck:blocked */

FuFileInputStream *
fu_file_input_stream_from_file(GFile *file,
			       GCancellable *cancellable,
			       GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
GFileInfo *
fu_file_input_stream_query_info(FuFileInputStream *stream,
				const gchar *attributes,
				GCancellable *cancellable,
				GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);

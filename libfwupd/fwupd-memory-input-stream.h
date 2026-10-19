/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-input-stream.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_MEMORY_INPUT_STREAM (fwupd_memory_input_stream_get_type())
G_DECLARE_FINAL_TYPE(FwupdMemoryInputStream,
		     fwupd_memory_input_stream,
		     FWUPD,
		     MEMORY_INPUT_STREAM,
		     FwupdInputStream)

FwupdMemoryInputStream *
fwupd_memory_input_stream_new_from_bytes(GBytes *bytes) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
FwupdMemoryInputStream *
fwupd_memory_input_stream_new_from_data(const void *data,
					gssize len,
					GDestroyNotify destroy) G_GNUC_WARN_UNUSED_RESULT;
FwupdMemoryInputStream *
fwupd_memory_input_stream_new_from_filename(const gchar *filename,
					    GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);

G_END_DECLS

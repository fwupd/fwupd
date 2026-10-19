/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-input-stream.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_FILE_INPUT_STREAM (fwupd_file_input_stream_get_type())
G_DECLARE_FINAL_TYPE(FwupdFileInputStream,
		     fwupd_file_input_stream,
		     FWUPD,
		     FILE_INPUT_STREAM,
		     FwupdInputStream)

FwupdFileInputStream *
fwupd_file_input_stream_new(const gchar *path, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
FwupdFileInputStream *
fwupd_file_input_stream_new_from_file(GFile *file, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);

G_END_DECLS

/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-input-stream.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_UNIX_INPUT_STREAM (fwupd_unix_input_stream_get_type())

G_DECLARE_FINAL_TYPE(FwupdUnixInputStream,
		     fwupd_unix_input_stream,
		     FWUPD,
		     UNIX_INPUT_STREAM,
		     FwupdInputStream)

FwupdUnixInputStream *
fwupd_unix_input_stream_new(gint fd, gboolean close_fd, GError **error) G_GNUC_WARN_UNUSED_RESULT;
FwupdUnixInputStream *
fwupd_unix_input_stream_new_from_filename(const gchar *filename,
					  GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
gint
fwupd_unix_input_stream_get_fd(FwupdUnixInputStream *stream) G_GNUC_NON_NULL(1);
gboolean
fwupd_unix_input_stream_require_seal(FwupdUnixInputStream *stream, GError **error);

G_END_DECLS

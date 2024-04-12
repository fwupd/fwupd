/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <gio/gunixinputstream.h>

#define FU_TYPE_UNIX_SEEKABLE_INPUT_STREAM (fu_unix_seekable_input_stream_get_type())

G_DECLARE_FINAL_TYPE(FuUnixSeekableInputStream,
		     fu_unix_seekable_input_stream,
		     FU,
		     UNIX_SEEKABLE_INPUT_STREAM,
		     GUnixInputStream)

GInputStream *
fu_unix_seekable_input_stream_new(gint fd, gboolean close_fd);

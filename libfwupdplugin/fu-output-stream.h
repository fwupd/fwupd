/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#include "fu-progress.h"

GOutputStream *
fu_output_stream_from_path(const gchar *path, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
gboolean
fu_output_stream_write_bytes(GOutputStream *stream,
			     GBytes *bytes,
			     FuProgress *progress,
			     GError **error) G_GNUC_NON_NULL(1, 2);

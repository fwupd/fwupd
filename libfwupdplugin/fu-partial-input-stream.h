/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#define FU_TYPE_PARTIAL_INPUT_STREAM (fu_partial_input_stream_get_type())

G_DECLARE_FINAL_TYPE(FuPartialInputStream,
		     fu_partial_input_stream,
		     FU,
		     PARTIAL_INPUT_STREAM,
		     GInputStream)

GInputStream *
fu_partial_input_stream_new(GInputStream *stream, gsize offset, gsize size, GError **error)
    G_GNUC_NON_NULL(1);

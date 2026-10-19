/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-input-stream.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_STREAM_INPUT_STREAM (fwupd_stream_input_stream_get_type())
G_DECLARE_FINAL_TYPE(FwupdStreamInputStream,
		     fwupd_stream_input_stream,
		     FWUPD,
		     STREAM_INPUT_STREAM,
		     FwupdInputStream)

FwupdStreamInputStream *
fwupd_stream_input_stream_new(GInputStream *stream) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);

G_END_DECLS

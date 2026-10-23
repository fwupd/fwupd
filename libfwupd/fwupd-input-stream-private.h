/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-input-stream.h"

G_BEGIN_DECLS

FwupdInputStreamImpl
fwupd_input_stream_get_impl(FwupdInputStream *stream) G_GNUC_NON_NULL(1);

G_END_DECLS

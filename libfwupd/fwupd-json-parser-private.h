/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-json-parser.h"

FwupdJsonNode *
fwupd_json_parser_load_from_stream_impl(FwupdJsonParser *self,
					FwupdInputStreamImpl stream_impl,
					FwupdJsonLoadFlags flags,
					GError **error)
    G_GNUC_NON_NULL(1, 2) G_GNUC_WARN_UNUSED_RESULT;

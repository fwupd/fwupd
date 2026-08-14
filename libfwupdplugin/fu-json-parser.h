/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <gio/gio.h>

#include "fwupd-json-node.h"

#include "fu-input-stream.h"

G_BEGIN_DECLS

FwupdJsonNode *
fu_json_parser_load_from_stream(FwupdJsonParser *self,
				FuInputStream *stream,
				FwupdJsonLoadFlags flags,
				GError **error) G_GNUC_NON_NULL(1, 2) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS

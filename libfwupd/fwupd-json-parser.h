/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <gio/gio.h>

#include "fwupd-json-node.h"

G_BEGIN_DECLS

#define FWUPD_TYPE_JSON_PARSER (fwupd_json_parser_get_type())
G_DECLARE_FINAL_TYPE(FwupdJsonParser, fwupd_json_parser, FWUPD, JSON_PARSER, GObject)

FwupdJsonParser *
fwupd_json_parser_new(void) G_GNUC_WARN_UNUSED_RESULT;

void
fwupd_json_parser_set_max_depth(FwupdJsonParser *self, guint max_depth) G_GNUC_NON_NULL(1);
void
fwupd_json_parser_set_max_items(FwupdJsonParser *self, guint max_items) G_GNUC_NON_NULL(1);

FwupdJsonNode *
fwupd_json_parser_load_from_stream(FwupdJsonParser *self,
				   GInputStream *stream,
				   FwupdJsonLoadFlags flags,
				   GError **error) G_GNUC_NON_NULL(1, 2) G_GNUC_WARN_UNUSED_RESULT;
FwupdJsonNode *
fwupd_json_parser_load_from_bytes(FwupdJsonParser *self,
				  GBytes *blob,
				  FwupdJsonLoadFlags flags,
				  GError **error) G_GNUC_NON_NULL(1, 2) G_GNUC_WARN_UNUSED_RESULT;
FwupdJsonNode *
fwupd_json_parser_load_from_data(FwupdJsonParser *self,
				 const gchar *text,
				 FwupdJsonLoadFlags flags,
				 GError **error) G_GNUC_NON_NULL(1, 2) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS

/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fwupd-codec.h"

void
fwupd_codec_json_append(JsonBuilder *builder, const gchar *key, const gchar *value)
    G_GNUC_NON_NULL(1, 2);
void
fwupd_codec_json_append_strv(JsonBuilder *builder, const gchar *key, gchar **value)
    G_GNUC_NON_NULL(1, 2);
void
fwupd_codec_json_append_int(JsonBuilder *builder, const gchar *key, guint64 value)
    G_GNUC_NON_NULL(1, 2);
void
fwupd_codec_json_append_bool(JsonBuilder *builder, const gchar *key, gboolean value)
    G_GNUC_NON_NULL(1, 2);

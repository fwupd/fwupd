/*
 * Copyright 2026 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

GByteArray *
fu_focal_moc_packet_new(guint8 cmd, const guint8 *buf, gsize bufsz, GError **error);
GByteArray *
fu_focal_moc_packet_parse(const guint8 *buf, gsize bufsz, GError **error);
gchar *
fu_focal_moc_version_parse(const guint8 *buf, gsize bufsz, gboolean *is_bootloader, GError **error);

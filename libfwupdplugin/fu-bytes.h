/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

gboolean
fu_bytes_set_contents(const gchar *filename,
		      GBytes *bytes,
		      GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);
GBytes *
fu_bytes_get_contents(const gchar *filename, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);
GBytes *
fu_bytes_align(GBytes *bytes, gsize blksz, gchar padval) G_GNUC_NON_NULL(1);
const guint8 *
fu_bytes_get_data_safe(GBytes *bytes, gsize *bufsz, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_bytes_is_empty(GBytes *bytes) G_GNUC_NON_NULL(1);
gboolean
fu_bytes_compare(GBytes *bytes1, GBytes *bytes2, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
GBytes *
fu_bytes_pad(GBytes *bytes, gsize sz, guint8 data) G_GNUC_NON_NULL(1);
GBytes *
fu_bytes_new_offset(GBytes *bytes, gsize offset, gsize length, GError **error)
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gchar *
fu_bytes_to_string(GBytes *bytes) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
GBytes *
fu_bytes_from_string(const gchar *str, GError **error) G_GNUC_NON_NULL(1);

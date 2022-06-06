/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

gboolean
fu_bytes_set_contents(const gchar *filename,
		      GBytes *bytes,
		      GError **error) G_GNUC_WARN_UNUSED_RESULT;
GBytes *
fu_bytes_get_contents(const gchar *filename, GError **error) G_GNUC_WARN_UNUSED_RESULT;
GBytes *
fu_bytes_get_contents_stream(GInputStream *stream,
			     gsize count,
			     GError **error) G_GNUC_WARN_UNUSED_RESULT;
GBytes *
fu_bytes_get_contents_fd(gint fd, gsize count, GError **error) G_GNUC_WARN_UNUSED_RESULT;
GBytes *
fu_bytes_align(GBytes *bytes, gsize blksz, gchar padval);
const guint8 *
fu_bytes_get_data_safe(GBytes *bytes, gsize *bufsz, GError **error);
gboolean
fu_bytes_is_empty(GBytes *bytes);
gboolean
fu_bytes_compare(GBytes *bytes1, GBytes *bytes2, GError **error) G_GNUC_WARN_UNUSED_RESULT;
GBytes *
fu_bytes_pad(GBytes *bytes, gsize sz);
GBytes *
fu_bytes_new_offset(GBytes *bytes, gsize offset, gsize length, GError **error)
    G_GNUC_WARN_UNUSED_RESULT;

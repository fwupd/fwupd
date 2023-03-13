/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

gsize
fu_struct_size(const gchar *fmt, GError **error);
GByteArray *
fu_struct_pack(const gchar *fmt, GError **error, ...);
gboolean
fu_struct_pack_into(const gchar *fmt, GError **error, guint8 *buf, gsize bufsz, gsize offset, ...);
gboolean
fu_struct_unpack(const gchar *fmt, GError **error, GByteArray *buf, ...);
gboolean
fu_struct_unpack_from(const gchar *fmt,
		      GError **error,
		      const guint8 *buf,
		      gsize bufsz,
		      gsize *offset,
		      ...);

/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

#include "fu-archive-struct.h"

#define FU_TYPE_ARCHIVE (fu_archive_get_type())

G_DECLARE_FINAL_TYPE(FuArchive, fu_archive, FU, ARCHIVE, GObject)

/**
 * FuArchiveFlags:
 * @FU_ARCHIVE_FLAG_NONE:		No flags set
 * @FU_ARCHIVE_FLAG_IGNORE_PATH:	Ignore any path component
 *
 * The flags to use when loading the archive.
 **/
typedef enum {
	FU_ARCHIVE_FLAG_NONE = 0,
	FU_ARCHIVE_FLAG_IGNORE_PATH = 1 << 0,
	/*< private >*/
	FU_ARCHIVE_FLAG_LAST
} FuArchiveFlags;

/**
 * FuArchiveIterateFunc:
 * @self: a #FuArchive
 * @filename: a filename
 * @bytes: the blob referenced by @filename
 * @user_data: user data
 * @error: a #GError or NULL
 *
 * The archive iteration callback.
 */
typedef gboolean (*FuArchiveIterateFunc)(FuArchive *self,
					 const gchar *filename,
					 GBytes *bytes,
					 gpointer user_data,
					 GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);

FuArchive *
fu_archive_new(GBytes *data, FuArchiveFlags flags, GError **error) G_GNUC_WARN_UNUSED_RESULT;
FuArchive *
fu_archive_new_stream(GInputStream *stream,
		      FuArchiveFlags flags,
		      GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fu_archive_add_entry(FuArchive *self, const gchar *fn, GBytes *blob) G_GNUC_NON_NULL(1, 2);
GBytes *
fu_archive_lookup_by_fn(FuArchive *self, const gchar *fn, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1, 2);
GByteArray *
fu_archive_write(FuArchive *self,
		 FuArchiveFormat format,
		 FuArchiveCompression compression,
		 GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_archive_iterate(FuArchive *self,
		   FuArchiveIterateFunc callback,
		   gpointer user_data,
		   GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1, 2);

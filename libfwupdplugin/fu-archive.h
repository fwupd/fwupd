/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib-object.h>

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
 * FuArchiveFormat:
 * @FU_ARCHIVE_FORMAT_UNKNOWN:			unknown
 * @FU_ARCHIVE_FORMAT_CPIO:			ASCII cpio
 * @FU_ARCHIVE_FORMAT_SHAR:			shar
 * @FU_ARCHIVE_FORMAT_TAR:			tar
 * @FU_ARCHIVE_FORMAT_USTAR:			POSIX ustar
 * @FU_ARCHIVE_FORMAT_PAX:			restricted POSIX pax interchange
 * @FU_ARCHIVE_FORMAT_GNUTAR:			GNU tar
 * @FU_ARCHIVE_FORMAT_ISO9660:			ISO9660
 * @FU_ARCHIVE_FORMAT_ZIP:			ZIP
 * @FU_ARCHIVE_FORMAT_AR:			ar (BSD)
 * @FU_ARCHIVE_FORMAT_AR_SVR4:			ar (GNU/SVR4)
 * @FU_ARCHIVE_FORMAT_MTREE:			mtree
 * @FU_ARCHIVE_FORMAT_RAW:			raw
 * @FU_ARCHIVE_FORMAT_XAR:			xar
 * @FU_ARCHIVE_FORMAT_7ZIP:			7-Zip
 * @FU_ARCHIVE_FORMAT_WARC:			WARC
 *
 * The archive format.
 **/
typedef enum {
	FU_ARCHIVE_FORMAT_UNKNOWN, /* Since: 1.8.1 */
	FU_ARCHIVE_FORMAT_CPIO,	   /* Since: 1.8.1 */
	FU_ARCHIVE_FORMAT_SHAR,	   /* Since: 1.8.1 */
	FU_ARCHIVE_FORMAT_TAR,	   /* Since: 1.8.1 */
	FU_ARCHIVE_FORMAT_USTAR,   /* Since: 1.8.1 */
	FU_ARCHIVE_FORMAT_PAX,	   /* Since: 1.8.1 */
	FU_ARCHIVE_FORMAT_GNUTAR,  /* Since: 1.8.1 */
	FU_ARCHIVE_FORMAT_ISO9660, /* Since: 1.8.1 */
	FU_ARCHIVE_FORMAT_ZIP,	   /* Since: 1.8.1 */
	FU_ARCHIVE_FORMAT_AR,	   /* Since: 1.8.1 */
	FU_ARCHIVE_FORMAT_AR_SVR4, /* Since: 1.8.1 */
	FU_ARCHIVE_FORMAT_MTREE,   /* Since: 1.8.1 */
	FU_ARCHIVE_FORMAT_RAW,	   /* Since: 1.8.1 */
	FU_ARCHIVE_FORMAT_XAR,	   /* Since: 1.8.1 */
	FU_ARCHIVE_FORMAT_7ZIP,	   /* Since: 1.8.1 */
	FU_ARCHIVE_FORMAT_WARC,	   /* Since: 1.8.1 */
} FuArchiveFormat;

/**
 * FuArchiveCompression:
 * @FU_ARCHIVE_COMPRESSION_UNKNOWN:			unknown
 * @FU_ARCHIVE_COMPRESSION_NONE:			none
 * @FU_ARCHIVE_COMPRESSION_GZIP:			Gzip (GNU Zip)
 * @FU_ARCHIVE_COMPRESSION_BZIP2:			Bzip2
 * @FU_ARCHIVE_COMPRESSION_COMPRESS:			compress (LZW)
 * @FU_ARCHIVE_COMPRESSION_LZMA:			LZMA
 * @FU_ARCHIVE_COMPRESSION_XZ:			XZ
 * @FU_ARCHIVE_COMPRESSION_UU:			Unix-to-Unix
 * @FU_ARCHIVE_COMPRESSION_LZIP:			LZip (LZMA)
 * @FU_ARCHIVE_COMPRESSION_LRZIP:			Long Range Zip (LZMA RZIP)
 * @FU_ARCHIVE_COMPRESSION_LZOP:			LZO
 * @FU_ARCHIVE_COMPRESSION_GRZIP:			GRZip
 * @FU_ARCHIVE_COMPRESSION_LZ4:			LZ4
 * @FU_ARCHIVE_COMPRESSION_ZSTD:			Zstd
 *
 * The archive compression.
 **/
typedef enum {
	FU_ARCHIVE_COMPRESSION_UNKNOWN,	 /* Since: 1.8.1 */
	FU_ARCHIVE_COMPRESSION_NONE,	 /* Since: 1.8.1 */
	FU_ARCHIVE_COMPRESSION_GZIP,	 /* Since: 1.8.1 */
	FU_ARCHIVE_COMPRESSION_BZIP2,	 /* Since: 1.8.1 */
	FU_ARCHIVE_COMPRESSION_COMPRESS, /* Since: 1.8.1 */
	FU_ARCHIVE_COMPRESSION_LZMA,	 /* Since: 1.8.1 */
	FU_ARCHIVE_COMPRESSION_XZ,	 /* Since: 1.8.1 */
	FU_ARCHIVE_COMPRESSION_UU,	 /* Since: 1.8.1 */
	FU_ARCHIVE_COMPRESSION_LZIP,	 /* Since: 1.8.1 */
	FU_ARCHIVE_COMPRESSION_LRZIP,	 /* Since: 1.8.1 */
	FU_ARCHIVE_COMPRESSION_LZOP,	 /* Since: 1.8.1 */
	FU_ARCHIVE_COMPRESSION_GRZIP,	 /* Since: 1.8.1 */
	FU_ARCHIVE_COMPRESSION_LZ4,	 /* Since: 1.8.1 */
	FU_ARCHIVE_COMPRESSION_ZSTD,	 /* Since: 1.8.1 */
} FuArchiveCompression;

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
					 GError **error) G_GNUC_WARN_UNUSED_RESULT;

const gchar *
fu_archive_format_to_string(FuArchiveFormat format) G_GNUC_WARN_UNUSED_RESULT;
FuArchiveFormat
fu_archive_format_from_string(const gchar *format) G_GNUC_WARN_UNUSED_RESULT;

FuArchiveCompression
fu_archive_compression_from_string(const gchar *compression) G_GNUC_WARN_UNUSED_RESULT;
const gchar *
fu_archive_compression_to_string(FuArchiveCompression compression) G_GNUC_WARN_UNUSED_RESULT;

FuArchive *
fu_archive_new(GBytes *data, FuArchiveFlags flags, GError **error) G_GNUC_WARN_UNUSED_RESULT;
void
fu_archive_add_entry(FuArchive *self, const gchar *fn, GBytes *blob);
GBytes *
fu_archive_lookup_by_fn(FuArchive *self, const gchar *fn, GError **error) G_GNUC_WARN_UNUSED_RESULT;
GBytes *
fu_archive_write(FuArchive *self,
		 FuArchiveFormat format,
		 FuArchiveCompression compression,
		 GError **error) G_GNUC_WARN_UNUSED_RESULT;
gboolean
fu_archive_iterate(FuArchive *self,
		   FuArchiveIterateFunc callback,
		   gpointer user_data,
		   GError **error) G_GNUC_WARN_UNUSED_RESULT;

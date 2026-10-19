/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * C header for the Rust CAB archive FFI functions in the fwupd-ffi crate.
 * This header is hand-written; keep it in sync with
 * rust/fwupd-ffi/src/cab.rs.
 */

#pragma once

#include <glib.h>

#include "fwupd-rust-streams.h"

G_BEGIN_DECLS

/**
 * FuRsCabArchive:
 *
 * Opaque handle to a parsed Microsoft Cabinet (CAB) archive.
 * Allocated by fu_rs_cab_archive_new() and freed by fu_rs_cab_archive_free().
 */
typedef struct FuRsCabArchive FuRsCabArchive;

/**
 * FuRsCabFileEntry:
 *
 * Opaque handle to a single file inside a #FuRsCabArchive.
 * The pointer is valid for the lifetime of the owning #FuRsCabArchive.
 * Do NOT free it.
 */
typedef struct FuRsCabFileEntry FuRsCabFileEntry;

/**
 * FuRsCabParseFlags:
 * @FU_RS_CAB_PARSE_FLAG_NONE: No flags set.
 * @FU_RS_CAB_PARSE_FLAG_IGNORE_CHECKSUM: Ignore data-block checksums.
 * @FU_RS_CAB_PARSE_FLAG_ONLY_BASENAME: Strip directory components; keep only the basename.
 */
typedef enum {
	FU_RS_CAB_PARSE_FLAG_NONE = 0,
	FU_RS_CAB_PARSE_FLAG_IGNORE_CHECKSUM = 1 << 0,
	FU_RS_CAB_PARSE_FLAG_ONLY_BASENAME = 1 << 1,
} FuRsCabParseFlags;

FuRsCabArchive *
fu_rs_cab_archive_new(const FuRsStreamImpl *stream,
		      gsize offset,
		      FuRsCabParseFlags flags,
		      GError **error);

void
fu_rs_cab_archive_free(FuRsCabArchive *archive);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuRsCabArchive, fu_rs_cab_archive_free);

gsize
fu_rs_cab_archive_get_nfiles(const FuRsCabArchive *archive);

gsize
fu_rs_cab_archive_get_size(const FuRsCabArchive *archive);

gboolean
fu_rs_cab_archive_was_compressed(const FuRsCabArchive *archive);

/**
 * fu_rs_cab_archive_get_file:
 * @archive: a #FuRsCabArchive
 * @idx: zero-based file index
 *
 * Returns a borrowed pointer to the @idx-th file entry.  The pointer is valid
 * for the lifetime of @archive; do not free it.
 *
 * Returns: a #FuRsCabFileEntry, or %NULL if @idx is out of bounds.
 */
FuRsCabFileEntry *
fu_rs_cab_archive_get_file(FuRsCabArchive *archive, gsize idx);

/**
 * fu_rs_cab_file_entry_get_name:
 *
 * Returns the NUL-terminated filename.  The pointer is borrowed from the
 * archive and valid for its lifetime.
 */
const gchar *
fu_rs_cab_file_entry_get_name(const FuRsCabFileEntry *file);

/**
 * fu_rs_cab_file_entry_get_name_as_unix_path:
 *
 * Returns the NUL-terminated filename in unix format, i.e. using
 * forward slashes only. On error %NULL is returned and the error
 * is set.
 *
 * Returns: (transfer full): A newly allocated string containing
 * the file name/path.
 */
GString *
fu_rs_cab_file_entry_get_name_as_unix_path(const FuRsCabFileEntry *file, GError **error);

gsize
fu_rs_cab_file_entry_get_size(const FuRsCabFileEntry *file);

/**
 * FuRsCabFileFlags:
 * @FU_RS_CAB_FILE_ATTR_NONE: No attribute set.
 * @FU_RS_CAB_FILE_ATTR_READONLY: Read-only file
 * @FU_RS_CAB_FILE_ATTR_HIDDEN: Hidden
 * @FU_RS_CAB_FILE_ATTR_SYSTEM: System file
 * @FU_RS_CAB_FILE_ATTR_EXEC: Run after extraction
 * @FU_RS_CAB_FILE_ATTR_FILENAME_IS_UTF8: Filename is UTF-8
 */
typedef enum {
	FU_RS_CAB_FILE_ATTR_NONE = 0,
	FU_RS_CAB_FILE_ATTR_READONLY = 1 << 0,
	FU_RS_CAB_FILE_ATTR_HIDDEN = 1 << 1,
	FU_RS_CAB_FILE_ATTR_SYSTEM = 1 << 2,
	FU_RS_CAB_FILE_ATTR_ARCH = 1 << 5,
	FU_RS_CAB_FILE_ATTR_EXEC = 1 << 6,
	FU_RS_CAB_FILE_ATTR_FILENAME_IS_UTF8 = 1 << 7,
} FuRsCabFileAttrs;

/**
 * fu_rs_cab_file_entry_get_attributes:
 *
 * Returns the MS-DOS file attributes as a 32-bit bitmask:
 */
guint32
fu_rs_cab_file_entry_get_attributes(const FuRsCabFileEntry *file);

/**
 * FuRsCabDateTime:
 * @year: full year (e.g. 2024), or 0 if no date is set
 * @month: month 1–12, or 0 if no date is set
 * @day: day 1–31, or 0 if no date is set
 * @hour: hour 0-23,
 * @minute: minute 0-59
 * @second: second 0-58 at 2 second resolution
 *
 * File creation datetime returned by fu_rs_cab_file_entry_get_datetime().
 *
 * If the file has no date or no time, the respective fields are zero.
 */
typedef struct {
	guint16 year;
	guint8 month;
	guint8 day;
	guint8 hour;
	guint8 minute;
	guint8 second;
} FuRsCabDateTime;

/**
 * fu_rs_cab_file_entry_get_datetime:
 *
 * Returns the date/time stamp of the file. If the file does
 * not have a date or time, the respective fields are zero.
 */
void
fu_rs_cab_file_entry_get_datetime(const FuRsCabFileEntry *file, FuRsCabDateTime *datetime);

/**
 * fu_rs_cab_file_entry_read:
 * @file: a #FuRsCabFileEntry
 * @buf: buffer to read into
 * @count: maximum number of bytes to read
 *
 * Reads up to @count bytes of uncompressed file content into @buf.
 *
 * Returns: number of bytes read (0 at EOF), or a negative errno on error.
 */
gssize
fu_rs_cab_file_entry_read(FuRsCabFileEntry *file, guint8 *buf, gsize count);

/**
 * FuRsCabBuilder:
 *
 * Opaque handle for building a Microsoft Cabinet (CAB) archive.
 * Allocated by fu_rs_cab_builder_new() and freed by fu_rs_cab_builder_free().
 */
typedef struct FuRsCabBuilder FuRsCabBuilder;

FuRsCabBuilder *
fu_rs_cab_builder_new(void);

void
fu_rs_cab_builder_free(FuRsCabBuilder *builder);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuRsCabBuilder, fu_rs_cab_builder_free);

/**
 * fu_rs_cab_builder_add_file:
 * @builder: a #FuRsCabBuilder
 * @name: filename or path (may use '/' or '\\' as path separators)
 * @data: file content, or %NULL when @data_len is 0
 * @data_len: length of @data in bytes
 * @datetime: The date/time stamp for this file, or %NULL to leave at zero
 * @attrs: A set of flags from the FuRsCabFileAttrs enum
 * @compress: Set to %TRUE if the file should be compressed
 * @error: (nullable): error location
 *
 * Appends a file to the builder.  The UTF-8 filename attribute is set
 * automatically when @name contains non-ASCII characters.
 *
 * Returns: %TRUE on success.
 */
gboolean
fu_rs_cab_builder_add_file(FuRsCabBuilder *builder,
			   const gchar *name,
			   const guint8 *data,
			   gsize data_len,
			   FuRsCabDateTime *datetime,
			   guint16 attrs,
			   gboolean compress,
			   GError **error);

/**
 * fu_rs_cab_builder_build:
 * @builder: a #FuRsCabBuilder
 * @error: (nullable): error location
 *
 * Serializes all files added with fu_rs_cab_builder_add_file() into an
 * uncompressed CAB archive. This function 'consumes' the builder, the
 * caller must not call any other func but fu_rs_cab_builder_free.
 *
 * Returns: (transfer full): the bytes representing this CAB
 */
GBytes *
fu_rs_cab_builder_build(FuRsCabBuilder *builder, GError **error);

G_END_DECLS

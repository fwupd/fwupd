/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

/* Opaque Rust CAB archive handle */
typedef struct FuRsCabArchive FuRsCabArchive; /* nocheck:name */

/* Parse / validate */
FuRsCabArchive *
fu_rs_cab_parse(void *stream, /* nocheck:name */
		int only_basename,
		int ignore_checksum,
		GError **error);
FuRsCabArchive *
fu_rs_cab_parse_bytes(const guint8 *buf, /* nocheck:name */
		      gsize bufsz,
		      int only_basename,
		      int ignore_checksum,
		      GError **error);
int
fu_rs_cab_validate(void *stream); /* nocheck:name */

/* Accessors */
gsize
fu_rs_cab_nfiles(const FuRsCabArchive *archive); /* nocheck:name */
const char *
fu_rs_cab_filename(const FuRsCabArchive *archive, gsize idx); /* nocheck:name */
const guint8 *
fu_rs_cab_file_data(const FuRsCabArchive *archive, gsize idx, gsize *len); /* nocheck:name */
int
fu_rs_cab_file_is_deferred(const FuRsCabArchive *archive, gsize idx); /* nocheck:name */
gsize
fu_rs_cab_file_nspans(const FuRsCabArchive *archive, gsize idx); /* nocheck:name */
int
fu_rs_cab_file_span(const FuRsCabArchive *archive, /* nocheck:name */
		    gsize idx,
		    gsize span_idx,
		    gsize *offset,
		    gsize *length);
gsize
fu_rs_cab_file_size(const FuRsCabArchive *archive, gsize idx); /* nocheck:name */
guint16
fu_rs_cab_file_date(const FuRsCabArchive *archive, gsize idx); /* nocheck:name */
guint16
fu_rs_cab_file_time(const FuRsCabArchive *archive, gsize idx); /* nocheck:name */
guint16
fu_rs_cab_file_year(const FuRsCabArchive *archive, gsize idx); /* nocheck:name */
guint8
fu_rs_cab_file_month(const FuRsCabArchive *archive, gsize idx); /* nocheck:name */
guint8
fu_rs_cab_file_day(const FuRsCabArchive *archive, gsize idx); /* nocheck:name */
guint8
fu_rs_cab_file_hour(const FuRsCabArchive *archive, gsize idx); /* nocheck:name */
guint8
fu_rs_cab_file_minute(const FuRsCabArchive *archive, gsize idx); /* nocheck:name */
guint8
fu_rs_cab_file_second(const FuRsCabArchive *archive, gsize idx); /* nocheck:name */
guint16
fu_rs_cab_file_attrs(const FuRsCabArchive *archive, gsize idx); /* nocheck:name */
int
fu_rs_cab_is_compressed(const FuRsCabArchive *archive); /* nocheck:name */

/* Write */
guint8 *
fu_rs_cab_write(const FuRsCabArchive *archive, /* nocheck:name */
		int compressed,
		gsize *out_len,
		GError **error);

/* Lifecycle */
FuRsCabArchive *
fu_rs_cab_new(void); /* nocheck:name */
void
fu_rs_cab_add_file(FuRsCabArchive *archive, /* nocheck:name */
		   const char *name,
		   const guint8 *data,
		   gsize data_len,
		   guint16 year,
		   guint8 month,
		   guint8 day,
		   guint8 hour,
		   guint8 minute,
		   guint8 second,
		   guint16 attrs);
void
fu_rs_cab_free(FuRsCabArchive *archive); /* nocheck:name */

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuRsCabArchive, fu_rs_cab_free) /* nocheck:name */

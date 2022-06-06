/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

/**
 * FuDumpFlags:
 * @FU_DUMP_FLAGS_NONE:			No flags set
 * @FU_DUMP_FLAGS_SHOW_ASCII:		Show ASCII in debugging dumps
 * @FU_DUMP_FLAGS_SHOW_ADDRESSES:	Show addresses in debugging dumps
 *
 * The flags to use when configuring debugging
 **/
typedef enum {
	FU_DUMP_FLAGS_NONE = 0,
	FU_DUMP_FLAGS_SHOW_ASCII = 1 << 0,
	FU_DUMP_FLAGS_SHOW_ADDRESSES = 1 << 1,
	/*< private >*/
	FU_DUMP_FLAGS_LAST
} FuDumpFlags;

void
fu_dump_raw(const gchar *log_domain, const gchar *title, const guint8 *data, gsize len);
void
fu_dump_full(const gchar *log_domain,
	     const gchar *title,
	     const guint8 *data,
	     gsize len,
	     guint columns,
	     FuDumpFlags flags);
void
fu_dump_bytes(const gchar *log_domain, const gchar *title, GBytes *bytes);

/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

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
} G_GNUC_FLAG_ENUM FuDumpFlags;

void
fu_dump_raw(const gchar *log_domain, const gchar *title, const guint8 *data, gsize len)
    G_GNUC_NON_NULL(1);
void
fu_dump_full(const gchar *log_domain,
	     const gchar *title,
	     const guint8 *data,
	     gsize len,
	     guint columns,
	     FuDumpFlags flags) G_GNUC_NON_NULL(1);
void
fu_dump_bytes(const gchar *log_domain, const gchar *title, GBytes *bytes) G_GNUC_NON_NULL(1, 3);

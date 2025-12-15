/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

#include "fu-dump-struct.h"

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

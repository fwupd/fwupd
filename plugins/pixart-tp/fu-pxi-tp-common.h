/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/**
 * fu_pxi_tp_common_hexdump_slice:
 * @p: pointer to byte array
 * @n: total available bytes
 * @maxbytes: maximum number of bytes to dump
 *
 * Returns a newly-allocated string containing a hex dump
 * of at most @maxbytes bytes, formatted as:
 *
 *    "AA BB CC DD ..."
 *
 * If @n or @maxbytes is zero, returns an empty string.
 *
 * The caller must free the return value with g_free().
 *
 * Returns: (transfer full): newly-allocated printable hex string
 */
gchar *
fu_pxi_tp_common_hexdump_slice(const guint8 *p, gsize n, gsize maxbytes);

G_END_DECLS

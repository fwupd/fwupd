/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupd.h>

G_BEGIN_DECLS

GBytes *
fu_lzma_decompress_bytes(GBytes *blob, guint64 memlimit, GError **error) G_GNUC_NON_NULL(1);
GBytes *
fu_lzma_compress_bytes(GBytes *blob, GError **error) G_GNUC_NON_NULL(1);

G_END_DECLS

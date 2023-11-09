/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <gio/gio.h>

GBytes *
fu_lzma_decompress_bytes(GBytes *blob, GError **error);
GBytes *
fu_lzma_compress_bytes(GBytes *blob, GError **error);

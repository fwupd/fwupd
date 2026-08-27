/*
 * Copyright 2026 Red Hat
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef enum {
	FWUPD_COMPRESSOR_FORMAT_RAW,
	FWUPD_COMPRESSOR_FORMAT_ZLIB,
	FWUPD_COMPRESSOR_FORMAT_GZIP,
} FwupdCompressorFormat;

G_END_DECLS

/*
 * Copyright 2025 Colin Kinloch <colin.kinloch@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <glib/glib.h>

// TODO: This is a hack to work around android bundles not supporting signed numbers
static guint64
fwupd_codec_variant_get_uint32(GVariant *value)
{
#ifdef HAS_BINDER_NDK
	return g_variant_get_int32(value);
#else
	return g_variant_get_uint32(value);
#endif
}

static guint64
fwupd_codec_variant_get_uint64(GVariant *value)
{
#ifdef HAS_BINDER_NDK
	return g_variant_get_int64(value);
#else
	return g_variant_get_uint64(value);
#endif
}

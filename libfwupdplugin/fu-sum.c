/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include "fu-mem-private.h"
#include "fu-sum.h"

guint8
fu_sum8(const guint8 *buf, gsize bufsz)
{
	guint8 checksum = 0;
	g_return_val_if_fail(buf != NULL, G_MAXUINT8);
	for (gsize i = 0; i < bufsz; i++)
		checksum += buf[i];
	return checksum;
}

gboolean
fu_sum8_safe(const guint8 *buf, gsize bufsz, gsize offset, gsize n, guint8 *value, GError **error)
{
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_memchk_read(bufsz, offset, n, error))
		return FALSE;
	if (value != NULL)
		*value = fu_sum8(buf + offset, n);
	return TRUE;
}

guint8
fu_sum8_bytes(GBytes *blob)
{
	g_return_val_if_fail(blob != NULL, G_MAXUINT8);
	if (g_bytes_get_size(blob) == 0)
		return 0;
	return fu_sum8(g_bytes_get_data(blob, NULL), g_bytes_get_size(blob));
}

guint16
fu_sum16(const guint8 *buf, gsize bufsz)
{
	guint16 checksum = 0;
	g_return_val_if_fail(buf != NULL, G_MAXUINT16);
	for (gsize i = 0; i < bufsz; i++)
		checksum += buf[i];
	return checksum;
}

gboolean
fu_sum16_safe(const guint8 *buf, gsize bufsz, gsize offset, gsize n, guint16 *value, GError **error)
{
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_memchk_read(bufsz, offset, n, error))
		return FALSE;
	if (value != NULL)
		*value = fu_sum16(buf + offset, n);
	return TRUE;
}

guint16
fu_sum16_bytes(GBytes *blob)
{
	g_return_val_if_fail(blob != NULL, G_MAXUINT16);
	return fu_sum16(g_bytes_get_data(blob, NULL), g_bytes_get_size(blob));
}

guint16
fu_sum16w(const guint8 *buf, gsize bufsz, FuEndianType endian)
{
	guint16 checksum = 0;
	g_return_val_if_fail(buf != NULL, G_MAXUINT16);
	g_return_val_if_fail(bufsz % 2 == 0, G_MAXUINT16);
	for (gsize i = 0; i < bufsz; i += 2)
		checksum += fu_memread_uint16(&buf[i], endian);
	return checksum;
}

guint16
fu_sum16w_bytes(GBytes *blob, FuEndianType endian)
{
	g_return_val_if_fail(blob != NULL, G_MAXUINT16);
	return fu_sum16w(g_bytes_get_data(blob, NULL), g_bytes_get_size(blob), endian);
}

guint32
fu_sum32(const guint8 *buf, gsize bufsz)
{
	guint32 checksum = 0;
	g_return_val_if_fail(buf != NULL, G_MAXUINT32);
	for (gsize i = 0; i < bufsz; i++)
		checksum += buf[i];
	return checksum;
}

guint32
fu_sum32_bytes(GBytes *blob)
{
	g_return_val_if_fail(blob != NULL, G_MAXUINT32);
	return fu_sum32(g_bytes_get_data(blob, NULL), g_bytes_get_size(blob));
}

guint32
fu_sum32w(const guint8 *buf, gsize bufsz, FuEndianType endian)
{
	guint32 checksum = 0;
	g_return_val_if_fail(buf != NULL, G_MAXUINT32);
	g_return_val_if_fail(bufsz % 4 == 0, G_MAXUINT32);
	for (gsize i = 0; i < bufsz; i += 4)
		checksum += fu_memread_uint32(&buf[i], endian);
	return checksum;
}

guint32
fu_sum32w_bytes(GBytes *blob, FuEndianType endian)
{
	g_return_val_if_fail(blob != NULL, G_MAXUINT32);
	return fu_sum32w(g_bytes_get_data(blob, NULL), g_bytes_get_size(blob), endian);
}

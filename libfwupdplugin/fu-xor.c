/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include "fu-mem-private.h"
#include "fu-xor.h"

guint8
fu_xor8(const guint8 *buf, gsize bufsz)
{
	guint8 tmp = 0;
	g_return_val_if_fail(buf != NULL, G_MAXUINT8);
	for (gsize i = 0; i < bufsz; i++)
		tmp ^= buf[i];
	return tmp;
}

gboolean
fu_xor8_safe(const guint8 *buf, gsize bufsz, gsize offset, gsize n, guint8 *value, GError **error)
{
	g_return_val_if_fail(buf != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	if (!fu_memchk_read(bufsz, offset, n, error))
		return FALSE;
	if (value != NULL)
		*value ^= fu_xor8(buf + offset, n);
	return TRUE;
}

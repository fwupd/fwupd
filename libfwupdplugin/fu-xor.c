/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuCommon"

#include "config.h"

#include "fu-mem-private.h"
#include "fu-xor.h"

/**
 * fu_xor8:
 * @buf: memory buffer
 * @bufsz: size of @buf
 *
 * Returns the bitwise XOR of all bytes in @buf.
 *
 * Returns: xor value
 *
 * Since: 2.1.1
 **/
guint8
fu_xor8(const guint8 *buf, gsize bufsz)
{
	guint8 tmp = 0;
	g_return_val_if_fail(buf != NULL, G_MAXUINT8);
	for (gsize i = 0; i < bufsz; i++)
		tmp ^= buf[i];
	return tmp;
}

/**
 * fu_xor8_safe:
 * @buf: source buffer
 * @bufsz: maximum size of @buf, typically `sizeof(buf)`
 * @offset: offset in bytes into @buf where XOR should start
 * @n: number of bytes to XOR from @buf
 * @value: (inout) (nullable): the initial value, and result
 * @error: (nullable): optional return location for an error
 *
 * Returns the bitwise XOR of all bytes in @buf in a safe way.
 *
 * You don't need to use this function in "obviously correct" cases, nor should
 * you use it when performance is a concern. Only use it when you're not sure if
 * malicious data from a device or firmware could cause memory corruption.
 *
 * Returns: %TRUE if @value was set, %FALSE otherwise
 *
 * Since: 2.1.1
 **/
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

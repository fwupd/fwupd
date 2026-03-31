/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-endian.h"

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
fu_xor8(const guint8 *buf, gsize bufsz) G_GNUC_NON_NULL(1);
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
    G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);

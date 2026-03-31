/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

#include "fu-mem.h"

/**
 * fu_memchk_read:
 * @bufsz: maximum size of a buffer, typically `sizeof(buf)`
 * @offset: offset in bytes
 * @n: number of bytes
 * @error: (nullable): optional return location for an error
 *
 * Works out if reading from a buffer is safe. Providing the buffer sizes allows us to check for
 * buffer overflow.
 *
 * You don't need to use this function in "obviously correct" cases, nor should
 * you use it when performance is a concern. Only us it when you're not sure if
 * malicious data from a device or firmware could cause memory corruption.
 *
 * Returns: %TRUE if the access is safe, %FALSE otherwise
 *
 * Since: 1.9.1
 **/
gboolean
fu_memchk_read(gsize bufsz, gsize offset, gsize n, GError **error);
/**
 * fu_memchk_write:
 * @bufsz: maximum size of a buffer, typically `sizeof(buf)`
 * @offset: offset in bytes
 * @n: number of bytes
 * @error: (nullable): optional return location for an error
 *
 * Works out if writing to a buffer is safe. Providing the buffer sizes allows us to check for
 * buffer overflow.
 *
 * You don't need to use this function in "obviously correct" cases, nor should
 * you use it when performance is a concern. Only us it when you're not sure if
 * malicious data from a device or firmware could cause memory corruption.
 *
 * Returns: %TRUE if the access is safe, %FALSE otherwise
 *
 * Since: 1.9.1
 **/
gboolean
fu_memchk_write(gsize bufsz, gsize offset, gsize n, GError **error);

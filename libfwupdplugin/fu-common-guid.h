/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <gio/gio.h>

/**
 * fu_common_guid_is_plausible:
 * @buf: a buffer of data
 *
 * Checks whether a chunk of memory looks like it could be a GUID.
 *
 * Returns: TRUE if it looks like a GUID, FALSE if not
 *
 * Since: 1.2.5
 **/
gboolean
fu_common_guid_is_plausible(const guint8 *buf);

/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

#include "fu-mem.h"

G_BEGIN_DECLS

gboolean
fu_memchk_read(gsize bufsz, gsize offset, gsize n, GError **error);
gboolean
fu_memchk_write(gsize bufsz, gsize offset, gsize n, GError **error);

G_END_DECLS

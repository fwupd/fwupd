/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

#include "fu-mem.h"

gboolean
fu_memchk_read(gsize bufsz, gsize offset, gsize n, GError **error);
gboolean
fu_memchk_write(gsize bufsz, gsize offset, gsize n, GError **error);

/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-crc.h"

guint32
fu_crc32_step(FuCrc32Kind kind, const guint8 *buf, gsize bufsz, guint32 crc);
guint32
fu_crc32_done(FuCrc32Kind kind, guint32 crc);

/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

guint32
fu_egis_moc_checksum_add(guint32 csum, const guint8 *buf, gsize bufsz);
guint16
fu_egis_moc_checksum_finish(guint32 csum);

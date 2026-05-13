/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-cab-firmware.h"

gboolean
fu_cab_firmware_compute_checksum(const guint8 *buf, gsize bufsz, guint32 *checksum, GError **error);

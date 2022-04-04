/*
 * Copyright (C) 2022 Andrii Dushko <andrii.dushko@developex.net>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <glib.h>

typedef enum {
	FU_CORSAIR_DEVICE_UNKNOWN = 0,
	FU_CORSAIR_DEVICE_MOUSE,
	FU_CORSAIR_DEVICE_DONGLE
} FuCorsairDeviceKind;

FuCorsairDeviceKind
fu_corsair_device_type_from_string(const gchar *kind);

const gchar *
fu_corsair_device_type_to_string(FuCorsairDeviceKind type);

guint32
fu_corsair_calculate_crc(const guint8 *data, guint32 data_len);

gchar *
fu_corsair_version_from_uint32(guint32 val);

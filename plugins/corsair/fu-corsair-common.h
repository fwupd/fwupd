/*
 * Copyright (C) 2022 Andrii Dushko <andrii.dushko@developex.net>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_CORSAIR_DEVICE_FLAG_LEGACY_ATTACH		(1 << 0)
#define FU_CORSAIR_DEVICE_FLAG_IS_SUBDEVICE		(1 << 1)
#define FU_CORSAIR_DEVICE_FLAG_NO_VERSION_IN_BOOTLOADER (1 << 2)

#define FU_CORSAIR_MAX_CMD_SIZE 1024

typedef enum {
	FU_CORSAIR_DEVICE_UNKNOWN = 0,
	FU_CORSAIR_DEVICE_MOUSE,
	FU_CORSAIR_DEVICE_RECEIVER
} FuCorsairDeviceKind;

typedef enum {
	FU_CORSAIR_BP_PROPERTY_MODE = 0x03,
	FU_CORSAIR_BP_PROPERTY_BATTERY_LEVEL = 0x0F,
	FU_CORSAIR_BP_PROPERTY_VERSION = 0x13,
	FU_CORSAIR_BP_PROPERTY_BOOTLOADER_VERSION = 0x14,
	FU_CORSAIR_BP_PROPERTY_SUBDEVICES = 0x36,
} FuCorsairBpProperty;

typedef enum {
	FU_CORSAIR_DEVICE_MODE_APPLICATION = 0x01,
	FU_CORSAIR_DEVICE_MODE_BOOTLOADER = 0x03
} FuCorsairDeviceMode;

FuCorsairDeviceKind
fu_corsair_device_type_from_string(const gchar *kind);

const gchar *
fu_corsair_device_type_to_string(FuCorsairDeviceKind type);

guint32
fu_corsair_calculate_crc(const guint8 *data, guint32 data_len);

gchar *
fu_corsair_version_from_uint32(guint32 val);

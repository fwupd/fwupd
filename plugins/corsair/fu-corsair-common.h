/*
 * Copyright 2022 Andrii Dushko <andrii.dushko@developex.net>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_CORSAIR_MAX_CMD_SIZE 1024

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

gchar *
fu_corsair_version_from_uint32(guint32 val);

/*
 * Copyright 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-steelseries-device.h"
#include "fu-steelseries-fizz-struct.h"

#define FU_TYPE_STEELSERIES_FIZZ (fu_steelseries_fizz_get_type())
G_DECLARE_FINAL_TYPE(FuSteelseriesFizz, fu_steelseries_fizz, FU, STEELSERIES_FIZZ, FuUsbDevice)

FuSteelseriesFizz *
fu_steelseries_fizz_new(FuDevice *self);

#define FU_STEELSERIES_FIZZ_BATTERY_LEVEL_CHARGING_BIT 0x80U
#define FU_STEELSERIES_FIZZ_BATTERY_LEVEL_STATUS_BITS  0x7fU

gboolean
fu_steelseries_fizz_reset(FuSteelseriesFizz *self,
			  gboolean tunnel,
			  FuSteelseriesFizzResetMode mode,
			  GError **error);
gboolean
fu_steelseries_fizz_get_crc32_fs(FuSteelseriesFizz *self,
				 gboolean tunnel,
				 guint8 fs,
				 guint8 id,
				 guint32 *calculated_crc,
				 guint32 *stored_crc,
				 GError **error);
FuFirmware *
fu_steelseries_fizz_read_firmware_fs(FuSteelseriesFizz *self,
				     gboolean tunnel,
				     guint8 fs,
				     guint8 id,
				     gsize size,
				     FuProgress *progress,
				     GError **error);
gboolean
fu_steelseries_fizz_write_firmware_fs(FuSteelseriesFizz *self,
				      gboolean tunnel,
				      guint8 fs,
				      guint8 id,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error);
gboolean
fu_steelseries_fizz_get_battery_level(FuSteelseriesFizz *self,
				      gboolean tunnel,
				      guint8 *level,
				      GError **error);
gboolean
fu_steelseries_fizz_get_connection_status(FuSteelseriesFizz *self,
					  FuSteelseriesFizzConnectionStatus *status,
					  GError **error);

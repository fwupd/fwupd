/*
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-steelseries-device.h"

#define FU_TYPE_STEELSERIES_FIZZ (fu_steelseries_fizz_get_type())
G_DECLARE_FINAL_TYPE(FuSteelseriesFizz,
		     fu_steelseries_fizz,
		     FU,
		     STEELSERIES_FIZZ,
		     FuSteelseriesDevice)

FuSteelseriesFizz *
fu_steelseries_fizz_new(FuDevice *self);

#define STEELSERIES_FIZZ_FILESYSTEM_RECEIVER 0x01U
#define STEELSERIES_FIZZ_FILESYSTEM_MOUSE    0x02U

#define STEELSERIES_FIZZ_CONNECTION_STATUS_NOT_CONNECTED 0x00U

#define STEELSERIES_FIZZ_RESET_MODE_NORMAL     0x00U
#define STEELSERIES_FIZZ_RESET_MODE_BOOTLOADER 0x01U

#define STEELSERIES_FIZZ_BATTERY_LEVEL_CHARGING_BIT 0x80U
#define STEELSERIES_FIZZ_BATTERY_LEVEL_STATUS_BITS  0x7fU

#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_MAIN_BOOT_ID	  0x01U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_FSDATA_FILE_ID	  0x02U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_FACTORY_SETTINGS_ID  0x03U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_MAIN_APP_ID	  0x04U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_BACKUP_APP_ID	  0x05U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_PROFILES_MOUSE_ID	  0x06U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_PROFILES_LIGHTING_ID 0x0fU
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_PROFILES_DEVICE_ID	  0x10U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_PROFILES_RESERVED_ID 0x11U
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_RECOVERY_ID	  0x0dU
#define STEELSERIES_FIZZ_RECEIVER_FILESYSTEM_FREE_SPACE_ID	  0xf1U

#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_SOFT_DEVICE_ID	0x00U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_PROFILES_MOUSE_ID	0x06U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_MAIN_APP_ID		0x07U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_BACKUP_APP_ID		0x08U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_MSB_DATA_ID		0x09U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_FACTORY_SETTINGS_ID	0x0aU
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_FSDATA_FILE_ID	0x0bU
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_MAIN_BOOT_ID		0x0cU
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_RECOVERY_ID		0x0eU
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_PROFILES_LIGHTING_ID	0x0fU
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_PROFILES_DEVICE_ID	0x10U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_FDS_PAGES_ID		0x12U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_PROFILES_BLUETOOTH_ID 0x13U
#define STEELSERIES_FIZZ_MOUSE_FILESYSTEM_FREE_SPACE_ID		0xf0U

gchar *
fu_steelseries_fizz_get_version(FuDevice *device, gboolean tunnel, GError **error);
gboolean
fu_steelseries_fizz_read_fs(FuDevice *device,
			    gboolean tunnel,
			    guint8 fs,
			    guint8 id,
			    guint8 *buf,
			    gsize bufsz,
			    FuProgress *progress,
			    GError **error);
gboolean
fu_steelseries_fizz_write_fs(FuDevice *device,
			     gboolean tunnel,
			     guint8 fs,
			     guint8 id,
			     const guint8 *buf,
			     gsize bufsz,
			     FuProgress *progress,
			     GError **error);
gboolean
fu_steelseries_fizz_erase_fs(FuDevice *device,
			     gboolean tunnel,
			     guint8 fs,
			     guint8 id,
			     GError **error);
gboolean
fu_steelseries_fizz_reset(FuDevice *device, gboolean tunnel, guint8 mode, GError **error);
gboolean
fu_steelseries_fizz_get_crc32_fs(FuDevice *device,
				 gboolean tunnel,
				 guint8 fs,
				 guint8 id,
				 guint32 *calculated_crc,
				 guint32 *stored_crc,
				 GError **error);
FuFirmware *
fu_steelseries_fizz_read_firmware_fs(FuDevice *device,
				     gboolean tunnel,
				     guint8 fs,
				     guint8 id,
				     gsize size,
				     FuProgress *progress,
				     GError **error);
gboolean
fu_steelseries_fizz_write_firmware_fs(FuDevice *device,
				      gboolean tunnel,
				      guint8 fs,
				      guint8 id,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error);
gboolean
fu_steelseries_fizz_get_battery_level(FuDevice *device,
				      gboolean tunnel,
				      guint8 *level,
				      GError **error);
gboolean
fu_steelseries_fizz_get_paired_status(FuDevice *device, guint8 *status, GError **error);
gboolean
fu_steelseries_fizz_get_connection_status(FuDevice *device, guint8 *status, GError **error);

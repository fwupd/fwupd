/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once
#include <fwupdplugin.h>
gboolean
fu_lenovo_accessory_ble_fwversion(FuBluezDevice *ble_device,
				  guint8 *major,
				  guint8 *minor,
				  guint8 *internal,
				  GError **error);

gboolean
fu_lenovo_accessory_ble_get_devicemode(FuBluezDevice *ble_device, guint8 *mode, GError **error);

gboolean
fu_lenovo_accessory_ble_dfu_set_devicemode(FuBluezDevice *ble_device, guint8 mode, GError **error);

gboolean
fu_lenovo_accessory_ble_dfu_exit(FuBluezDevice *ble_device, guint8 exit_code, GError **error);

gboolean
fu_lenovo_accessory_ble_dfu_attribute(FuBluezDevice *ble_device,
				      guint8 *major_ver,
				      guint8 *minor_ver,
				      guint16 *product_pid,
				      guint8 *processor_id,
				      guint32 *app_max_size,
				      guint32 *page_size,
				      GError **error);

gboolean
fu_lenovo_accessory_ble_dfu_prepare(FuBluezDevice *ble_device,
				    guint8 file_type,
				    guint32 start_address,
				    guint32 end_address,
				    guint32 crc32,
				    GError **error);

gboolean
fu_lenovo_accessory_ble_dfu_file(FuBluezDevice *ble_device,
				 guint8 file_type,
				 guint32 address,
				 const guint8 *file_data,
				 guint8 block_size,
				 GError **error);

gboolean
fu_lenovo_accessory_ble_dfu_crc(FuBluezDevice *ble_device, guint32 *crc32, GError **error);

gboolean
fu_lenovo_accessory_ble_dfu_entry(FuBluezDevice *ble_device, GError **error);

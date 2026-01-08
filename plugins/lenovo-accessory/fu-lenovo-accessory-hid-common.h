/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once
#include <fwupdplugin.h>
gboolean
fu_lenovo_accessory_hid_fwversion(FuHidrawDevice *hidraw_device,
				  guint8 *major,
				  guint8 *minor,
				  guint8 *internal,
				  GError **error);

gboolean
fu_lenovo_accessory_hid_dfu_set_devicemode(FuHidrawDevice *hidraw_device,
					   guint8 mode,
					   GError **error);

gboolean
fu_lenovo_accessory_hid_dfu_exit(FuHidrawDevice *hidraw_device, guint8 exit_code, GError **error);

gboolean
fu_lenovo_accessory_hid_dfu_attribute(FuHidrawDevice *hidraw_device,
				      guint8 *major_ver,
				      guint8 *minor_ver,
				      guint16 *product_pid,
				      guint8 *processor_id,
				      guint32 *app_max_size,
				      guint32 *page_size,
				      GError **error);

gboolean
fu_lenovo_accessory_hid_dfu_prepare(FuHidrawDevice *hidraw_device,
				    guint8 file_type,
				    guint32 start_address,
				    guint32 end_address,
				    guint32 crc32,
				    GError **error);

gboolean
fu_lenovo_accessory_hid_dfu_file(FuHidrawDevice *hidraw_device,
				 guint8 file_type,
				 guint32 address,
				 const guint8 *file_data,
				 guint8 block_size,
				 GError **error);

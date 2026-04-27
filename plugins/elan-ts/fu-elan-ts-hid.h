/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include <fwupdplugin.h>

#include "fu-elan-ts-struct.h"

gboolean
fu_elan_ts_hid_write_vendor_command(FuHidrawDevice *device,
				    const guint8 *p_vendor_cmd_buf,
				    gsize vendor_cmd_len,
				    GError **error);

gboolean
fu_elan_ts_hid_write_command(FuHidrawDevice *device,
			     const guint8 *p_cmd_buf,
			     gsize cmd_len,
			     GError **error);

gboolean
fu_elan_ts_hid_read_data(FuHidrawDevice *device,
			 guint8 *p_data_buf,
			 gsize data_len,
			 gboolean filter,
			 GError **error);

gboolean
fu_elan_ts_hid_read_hello_packet_bc_version_with_retry(FuHidrawDevice *device,
						       guint8 *p_hello_packet,
						       guint16 *p_bc_version,
						       GError **error);

gboolean
fu_elan_ts_hid_read_boot_code_version(FuHidrawDevice *device,
				      guint16 *p_bc_version,
				      GError **error);

gboolean
fu_elan_ts_hid_read_fw_id(FuHidrawDevice *device, guint16 *p_fw_id, GError **error);

gboolean
fu_elan_ts_hid_read_fw_version(FuHidrawDevice *device, guint16 *p_fw_version, GError **error);

gboolean
fu_elan_ts_hid_read_test_solution_version(FuHidrawDevice *device,
					  guint16 *p_test_solution_version,
					  GError **error);

gboolean
fu_elan_ts_hid_read_remark_id(FuHidrawDevice *device,
			      FuElanTsState touch_state,
			      guint16 fw_version,
			      guint16 bc_version,
			      guint16 *p_remark_id,
			      GError **error);

gboolean
fu_elan_ts_hid_read_info_page_with_retry(FuHidrawDevice *device,
					 guint8 *p_info_page_buf,
					 gsize info_page_buf_size,
					 GError **error);

gboolean
fu_elan_ts_hid_unlock_flash(FuHidrawDevice *device, GError **error);

gboolean
fu_elan_ts_hid_enter_iap_mode(FuHidrawDevice *device, GError **error);

gboolean
fu_elan_ts_hid_check_i2c_address(FuHidrawDevice *device, GError **error);

gboolean
fu_elan_ts_hid_write_frame_data(FuHidrawDevice *device,
				guint16 data_offset,
				gsize data_len,
				const guint8 *p_frame_buf,
				GError **error);

gboolean
fu_elan_ts_hid_send_flash_write_command(FuHidrawDevice *device, GError **error);

gboolean
fu_elan_ts_hid_read_flash_write_response(FuHidrawDevice *device,
					 guint16 *p_response,
					 GError **error);

gboolean
fu_elan_ts_hid_device_recalibrate_with_retry(FuHidrawDevice *device, GError **error);

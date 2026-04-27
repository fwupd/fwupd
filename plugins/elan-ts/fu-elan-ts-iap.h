/*
 * Copyright 2026 Elan Microelectronics Corporation <paul.liang@emc.com.tw>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#pragma once

#include <fwupdplugin.h>

#include "fu-elan-ts-struct.h"

gboolean
fu_elan_ts_iap_read_and_update_info_page(FuHidrawDevice *device,
					 guint8 solution_id,
					 guint8 *p_fw_page_buf,
					 gsize fw_page_buf_size,
					 GError **error);

gboolean
fu_elan_ts_iap_check_remark_id(FuHidrawDevice *device,
			       FuFirmware *firmware,
			       FuElanTsState touch_state,
			       guint16 fw_version,
			       guint16 bc_version,
			       GError **error);

gboolean
fu_elan_ts_iap_switch_to_boot_code(FuHidrawDevice *device, gboolean recovery, GError **error);

gboolean
fu_elan_ts_iap_write_firmware_pages(FuHidrawDevice *device,
				    const guint8 *p_pages_buf,
				    gsize pages_buf_size,
				    GError **error);

/*
 * Copyright 2021 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_PXI_DEVICE_FLAG_IS_HPAC "is-hpac"

#define PXI_HID_WIRELESS_DEV_OTA_REPORT_ID 0x03

#define FU_PXI_BLE_DEVICE_RF_CMD_CODE	0x65u
#define FU_PXI_BLE_DEVICE_RF_CMD_HID_SN 0x0
#define FU_PXI_BLE_DEVICE_RF_CMD_HID_SN 0x0

#define FU_PXI_WIRELESS_DEVICE_TARGET_RECEIVER 0
#define FU_PXI_RECEIVER_DEVICE_OTA_BUF_SZ      64 /* bytes */
#define FU_PXI_DEVICE_MODEL_NAME_LEN	       12 /* bytes */

#define FU_PXI_DEVICE_OBJECT_SIZE_MAX 4096 /* bytes */

#define FU_PXI_WIRELESS_DEVICE_RETRY_MAXIMUM 1000

#define FU_PXI_DEVICE_IOCTL_TIMEOUT 5000 /* ms */

/* pixart device model structure */
struct ota_fw_dev_model {
	guint8 status;
	guint8 name[FU_PXI_DEVICE_MODEL_NAME_LEN];
	guint8 type;
	guint8 target;
	guint8 version[5];
	guint16 checksum;
};

/* pixart fw info structure */
struct ota_fw_info {
	guint8 status;
	guint8 version[5];
	guint16 checksum;
};

struct ota_fw_state {
	guint8 status;
	guint8 new_flow;
	guint16 offset;
	guint16 checksum;
	guint32 max_object_size;
	guint16 mtu_size;
	guint16 prn_threshold;
	guint8 spec_check_result;
};

gboolean
fu_pxi_composite_receiver_cmd(guint8 opcode,
			      guint8 sn,
			      guint8 target,
			      GByteArray *wireless_mod_cmd,
			      GByteArray *ota_cmd,
			      GError **error);

void
fu_pxi_ota_fw_state_to_string(struct ota_fw_state *fwstate, guint idt, GString *str);
gboolean
fu_pxi_ota_fw_state_parse(struct ota_fw_state *fwstate,
			  const guint8 *buf,
			  gsize bufsz,
			  gsize offset,
			  GError **error);

gchar *
fu_pxi_hpac_version_info_parse(const guint16 hpac_ver);

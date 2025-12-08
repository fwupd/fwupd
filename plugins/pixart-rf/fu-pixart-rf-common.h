/*
 * Copyright 2021 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_PIXART_RF_DEVICE_FLAG_IS_HPAC "is-hpac"

#define PIXART_RF_HID_WIRELESS_DEV_OTA_REPORT_ID 0x03

#define FU_PIXART_RF_BLE_DEVICE_RF_CMD_CODE   0x65u
#define FU_PIXART_RF_BLE_DEVICE_RF_CMD_HID_SN 0x0
#define FU_PIXART_RF_BLE_DEVICE_RF_CMD_HID_SN 0x0

#define FU_PIXART_RF_WIRELESS_DEVICE_TARGET_RECEIVER 0
#define FU_PIXART_RF_RECEIVER_DEVICE_OTA_BUF_SZ	     64 /* bytes */
#define FU_PIXART_RF_DEVICE_MODEL_NAME_LEN	     12 /* bytes */

#define FU_PIXART_RF_DEVICE_OBJECT_SIZE_MAX 4096 /* bytes */

#define FU_PIXART_RF_WIRELESS_DEVICE_RETRY_MAXIMUM 1000

#define FU_PIXART_RF_DEVICE_IOCTL_TIMEOUT 5000 /* ms */

/* pixart device model structure */
typedef struct {
	guint8 status;
	guint8 name[FU_PIXART_RF_DEVICE_MODEL_NAME_LEN];
	guint8 type;
	guint8 target;
	guint8 version[5];
	guint16 checksum;
} FuPixartRfOtaFwDevModel;

/* pixart fw info structure */
typedef struct {
	guint8 status;
	guint8 version[5];
	guint16 checksum;
} FuPixartRfOtaFwInfo;

typedef struct {
	guint8 status;
	guint8 new_flow;
	guint16 offset;
	guint16 checksum;
	guint32 max_object_size;
	guint16 mtu_size;
	guint16 prn_threshold;
	guint8 spec_check_result;
} FuPixartRfOtaFwState;

gboolean
fu_pixart_rf_composite_receiver_cmd(guint8 opcode,
				    guint8 sn,
				    guint8 target,
				    GByteArray *wireless_mod_cmd,
				    GByteArray *ota_cmd,
				    GError **error);

void
fu_pixart_rf_ota_fw_state_to_string(FuPixartRfOtaFwState *fwstate, guint idt, GString *str);
gboolean
fu_pixart_rf_ota_fw_state_parse(FuPixartRfOtaFwState *fwstate,
				const guint8 *buf,
				gsize bufsz,
				gsize offset,
				GError **error);

gchar *
fu_pixart_rf_hpac_version_info_parse(const guint16 hpac_ver);

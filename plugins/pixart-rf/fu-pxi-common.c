/*
 * Copyright (C) 2021 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-pxi-common.h"

guint8
fu_pxi_common_sum8 (const guint8 *buf, gsize bufsz)
{
	guint8 checksum = 0;
	for (gsize idx = 0; idx < bufsz; idx++)
		checksum += (guint8) buf[idx];
	return checksum;
}

guint16
fu_pxi_common_sum16 (const guint8 *buf, gsize bufsz)
{
	guint16 checksum = 0;
	for (gsize idx = 0; idx < bufsz; idx++)
		checksum += (guint8) buf[idx];
	return checksum;
}

const gchar *
fu_pxi_spec_check_result_to_string (guint8 spec_check_result)
{
	if (spec_check_result == OTA_SPEC_CHECK_OK)
		return "ok";
	if (spec_check_result == OTA_FW_OUT_OF_BOUNDS)
		return "fw-out-of-bounds";
	if (spec_check_result == OTA_PROCESS_ILLEGAL)
		return "process-illegal";
	if (spec_check_result == OTA_RECONNECT)
		return "reconnect";
	if (spec_check_result == OTA_FW_IMG_VERSION_ERROR)
		return "fw-img-version-error";
	if (spec_check_result == OTA_DEVICE_LOW_BATTERY)
		return "device-low-battery";
	return NULL;
}

const gchar *
fu_pxi_receiver_cmd_result_to_string (guint8 result)
{
	if (result == OTA_RSP_OK)
		return "ok";
	if (result == OTA_RSP_FINISH)
		return "ota-response-finish";
	if (result == OTA_RSP_FAIL)
		return "ota-response-fail";
	if (result == OTA_RSP_CODE_ERROR)
		return "ota-response-error";
	return NULL;
}

void
fu_pxi_ota_fw_state_to_string (struct ota_fw_state *fwstate, guint idt, GString *str)
{
	fu_common_string_append_kx (str, idt, "Status", fwstate->status);
	fu_common_string_append_kx (str, idt, "NewFlow", fwstate->new_flow);
	fu_common_string_append_kx (str, idt, "CurrentObjectOffset", fwstate->offset);
	fu_common_string_append_kx (str, idt, "CurrentChecksum", fwstate->checksum);
	fu_common_string_append_kx (str, idt, "MaxObjectSize", fwstate->max_object_size);
	fu_common_string_append_kx (str, idt, "MtuSize", fwstate->mtu_size);
	fu_common_string_append_kx (str, idt, "PacketReceiptNotificationThreshold", fwstate->prn_threshold);
	fu_common_string_append_kv (str, idt, "SpecCheckResult",
				    fu_pxi_spec_check_result_to_string (fwstate->spec_check_result));
}

gboolean
fu_pxi_ota_fw_state_parse (struct ota_fw_state *fwstate,
			   const guint8 *buf,
			   gsize bufsz,
			   gsize offset,
			   GError **error)
{
	if (!fu_common_read_uint8_safe (buf, bufsz, offset + 0x00,
					&fwstate->status, error))
		return FALSE;
	if (!fu_common_read_uint8_safe (buf, bufsz, offset + 0x01,
					&fwstate->new_flow, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (buf, bufsz, offset + 0x2,
					 &fwstate->offset, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (buf, bufsz, offset + 0x4,
					 &fwstate->checksum, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint32_safe (buf, bufsz, offset + 0x06,
					 &fwstate->max_object_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (buf, bufsz, offset + 0x0A,
					 &fwstate->mtu_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint16_safe (buf, bufsz, offset + 0x0C,
					 &fwstate->prn_threshold, G_LITTLE_ENDIAN, error))
		return FALSE;
	if (!fu_common_read_uint8_safe (buf, bufsz, offset + 0x0E,
					&fwstate->spec_check_result, error))
		return FALSE;

	/* success */
	return TRUE;
}
gboolean
fu_pxi_composite_receiver_cmd (guint8 opcode, guint8 sn, guint8 target,
			       GByteArray *wireless_mod_cmd,
			       GByteArray *ota_cmd,
			       GError **error)
{
	guint8 checksum = 0x0;
	guint8 hid_sn = sn;
	guint8 len = 0x0;
	guint8 ota_sn = sn + 1;
	guint8 rf_cmd_code = FU_PXI_BLE_DEVICE_RF_CMD_CODE;
	guint8 rid = PXI_HID_WIRELESS_DEV_OTA_REPORT_ID;

	if (ota_cmd == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "ota cmd is NULL");
		return FALSE;
	}

	/* append ota dispatch header */
	fu_byte_array_append_uint8 (wireless_mod_cmd, opcode); 	/* wirelss module ota op code */
	fu_byte_array_append_uint8 (wireless_mod_cmd, ota_sn);	/* wirelss module ota command sn */

	/* append ota command length and content  */
	for(guint idx = 0; idx < ota_cmd->len; idx++)
		fu_byte_array_append_uint8 (wireless_mod_cmd, ota_cmd->data[idx]);

	/* append target of wireless module and hid command serial number */
	g_byte_array_prepend (wireless_mod_cmd, &target, 0x01); /* target */
	g_byte_array_prepend (wireless_mod_cmd, &hid_sn, 0x01); /* hid command sn */

	/* prepend length and rf command code */
	len = wireless_mod_cmd->len;
	g_byte_array_prepend (wireless_mod_cmd, &len, 0x01);
	g_byte_array_prepend (wireless_mod_cmd, &rf_cmd_code, 0x01); /* command code */

	/* prepend checksum */
	checksum = fu_pxi_common_sum8 (wireless_mod_cmd->data ,wireless_mod_cmd->len);
	g_byte_array_prepend (wireless_mod_cmd, &checksum, 0x01);

	/* prepend feature report id */
	g_byte_array_prepend (wireless_mod_cmd, &rid, 0x01);
	return TRUE;
}

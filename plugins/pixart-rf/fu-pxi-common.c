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

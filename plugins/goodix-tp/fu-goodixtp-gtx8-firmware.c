/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-goodixtp-common.h"
#include "fu-goodixtp-gtx8-firmware.h"

struct _FuGoodixtpGtx8Firmware {
	FuGoodixtpFirmware parent_instance;
};

G_DEFINE_TYPE(FuGoodixtpGtx8Firmware, fu_goodixtp_gtx8_firmware, FU_TYPE_GOODIXTP_FIRMWARE)

#define GTX8_FW_INFO_OFFSET 32
#define GTX8_FW_DATA_OFFSET 256

gboolean
fu_goodixtp_gtx8_firmware_parse(FuGoodixtpFirmware *self,
				GBytes *fw,
				guint8 sensor_id,
				GError **error)
{
	gboolean has_config = FALSE;
	guint32 version;
	guint16 checksum = 0;
	gint subsys_num;
	gint i;
	gint sub_cfg_num;
	guint sub_cfg_info_pos;
	guint cfg_offset;
	guint8 sub_cfg_id;
	guint sub_cfg_len;
	guint32 firmware_size;
	guint8 *tmp_data;
	guint32 sub_fw_info_pos = GTX8_FW_INFO_OFFSET;
	guint8 cfg_ver = 0;
	guint32 fw_image_offset = GTX8_FW_DATA_OFFSET;
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	if (!fu_memread_uint32_safe(buf, bufsz, 0, &firmware_size, G_BIG_ENDIAN, error))
		return FALSE;
	if ((gsize)firmware_size + 6 != bufsz) {
		g_debug("Check file len unequal %d != %d, this bin may contain config",
			(gint)firmware_size + 6,
			(gint)bufsz);
		has_config = TRUE;
	}

	for (i = 6; i < (gint)firmware_size + 6; i++)
		checksum += buf[i];
	if (checksum != fu_memread_uint16(&buf[4], G_BIG_ENDIAN)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware bin checksum error");
		return FALSE;
	}

	if (has_config) {
		guint16 cfg_packlen = fu_memread_uint16(&buf[firmware_size + 6], G_BIG_ENDIAN);
		if ((gint)(bufsz - firmware_size - 6) != (gint)cfg_packlen + 6) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "config pack len error");
			return FALSE;
		}

		for (i = firmware_size + 12, checksum = 0; i < (gint)bufsz; i++)
			checksum += buf[i];
		if (checksum != fu_memread_uint16(&buf[firmware_size + 10], G_BIG_ENDIAN)) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "config pack checksum error");
			return FALSE;
		}
		sub_cfg_num = buf[firmware_size + 9];
		if (sub_cfg_num == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "sub_cfg_num is 0");
			return FALSE;
		}
		sub_cfg_info_pos = firmware_size + 12;
		cfg_offset = firmware_size + 6 + 64;
		for (i = 0; i < sub_cfg_num; i++) {
			sub_cfg_id = buf[sub_cfg_info_pos];
			sub_cfg_len = fu_memread_uint16(&buf[sub_cfg_info_pos + 1], G_BIG_ENDIAN);
			if (sensor_id == sub_cfg_id) {
				const guint8 *fw_data = fu_goodixtp_firmware_get_data(self);
				fu_goodixtp_add_chunk_data(self,
							   3,
							   0x1E000,
							   (guint8 *)buf + cfg_offset,
							   sub_cfg_len);
				cfg_ver = fw_data[0];
				g_debug("Find a cfg match sensorID:ID=%d, cfg version=%d",
					sensor_id,
					cfg_ver);
				break;
			}
			cfg_offset += sub_cfg_len;
			sub_cfg_info_pos += 3;
		}
		g_debug("sub_cfg_ver:0x%02x", cfg_ver);
	}

	subsys_num = buf[27];
	if (subsys_num == 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "subsys_num is 0, exit");
		return FALSE;
	}

	for (i = 0; i < subsys_num; i++) {
		guint8 tmp_type = buf[sub_fw_info_pos];
		guint32 tmp_size = fu_memread_uint32(&buf[sub_fw_info_pos + 1], G_BIG_ENDIAN);
		guint32 tmp_addr = fu_memread_uint16(&buf[sub_fw_info_pos + 5], G_BIG_ENDIAN) << 8;
		guint32 offset = 0;

		tmp_data = (guint8 *)buf + fw_image_offset;
		fw_image_offset += tmp_size;
		sub_fw_info_pos += 8;
		if (tmp_type == 0x01) {
			g_debug("Sub firmware type does not math:type[1]");
			continue;
		}

		for (gint j = 0; j < (gint)(tmp_size / RAM_BUFFER_SIZE); j++) {
			fu_goodixtp_add_chunk_data(self,
						   tmp_type,
						   tmp_addr + offset,
						   tmp_data + offset,
						   RAM_BUFFER_SIZE);
			offset += RAM_BUFFER_SIZE;
		}
		if ((tmp_size % RAM_BUFFER_SIZE) != 0) {
			guint32 remain_len = tmp_size % RAM_BUFFER_SIZE;
			fu_goodixtp_add_chunk_data(self,
						   tmp_type,
						   tmp_addr + offset,
						   tmp_data + offset,
						   remain_len);
		}
	}
	version = (buf[25] << 16) | (buf[26] << 8) | cfg_ver;
	fu_goodixtp_firmware_set_version(self, version);
	return TRUE;
}

static void
fu_goodixtp_gtx8_firmware_init(FuGoodixtpGtx8Firmware *self)
{
}

static void
fu_goodixtp_gtx8_firmware_class_init(FuGoodixtpGtx8FirmwareClass *klass)
{
}

FuFirmware *
fu_goodixtp_gtx8_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_GOODIXTP_GTX8_FIRMWARE, NULL));
}

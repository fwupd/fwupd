/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-goodixtp-common.h"
#include "fu-goodixtp-gtx8-firmware.h"
#include "fu-goodixtp-struct.h"

struct _FuGoodixtpGtx8Firmware {
	FuGoodixtpFirmware parent_instance;
};

G_DEFINE_TYPE(FuGoodixtpGtx8Firmware, fu_goodixtp_gtx8_firmware, FU_TYPE_GOODIXTP_FIRMWARE)

#define GTX8_FW_DATA_OFFSET 256

gboolean
fu_goodixtp_gtx8_firmware_parse(FuGoodixtpFirmware *self,
				GBytes *fw,
				guint8 sensor_id,
				GError **error)
{
	gboolean has_config = FALSE;
	gsize bufsz = 0;
	guint16 checksum = 0;
	guint32 firmware_size = 0;
	guint32 version;
	guint8 cfg_ver = 0;
	guint8 subsys_num;
	guint sub_cfg_info_pos;
	guint32 offset_hdr;
	guint32 offset_payload = GTX8_FW_DATA_OFFSET;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GByteArray) st = NULL;

	st = fu_struct_goodix_gtx8_hdr_parse(buf, bufsz, 0x0, error);
	if (st == NULL)
		return FALSE;
	firmware_size = fu_struct_goodix_gtx8_hdr_get_firmware_size(st);
	if (firmware_size < 6 || firmware_size > G_MAXUINT32 - GTX8_FW_DATA_OFFSET) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid firmware size");
		return FALSE;
	}
	if ((gsize)firmware_size + 6 != bufsz) {
		g_debug("check file len unequal 0x%x != 0x%x, this bin may contain config",
			(guint)firmware_size + 6,
			(guint)bufsz);
		has_config = TRUE;
	}

	/* verify checksum */
	for (guint i = 6; i < (guint)firmware_size + 6; i++) {
		guint8 tmp_val = 0;
		if (!fu_memread_uint8_safe(buf, bufsz, i, &tmp_val, error))
			return FALSE;
		checksum += tmp_val;
	}
	if (checksum != fu_struct_goodix_gtx8_hdr_get_checksum(st)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "checksum invalid");
		return FALSE;
	}

	if (has_config) {
		guint16 cfg_packlen = 0;
		guint cfg_offset;
		guint8 sub_cfg_num;
		guint16 read_cksum;

		if (!fu_memread_uint16_safe(buf,
					    bufsz,
					    firmware_size + 6,
					    &cfg_packlen,
					    G_BIG_ENDIAN,
					    error))
			return FALSE;
		if ((gint)(bufsz - firmware_size - 6) != (gint)cfg_packlen + 6) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "config pack len error");
			return FALSE;
		}

		checksum = 0;
		for (guint i = firmware_size + 12; i < (guint)bufsz; i++) {
			guint8 tmp_val;
			if (!fu_memread_uint8_safe(buf, bufsz, i, &tmp_val, error))
				return FALSE;
			checksum += tmp_val;
		}
		if (!fu_memread_uint16_safe(buf,
					    bufsz,
					    firmware_size + 10,
					    &read_cksum,
					    G_BIG_ENDIAN,
					    error))
			return FALSE;
		if (checksum != read_cksum) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "config pack checksum error");
			return FALSE;
		}
		if (!fu_memread_uint8_safe(buf, bufsz, firmware_size + 9, &sub_cfg_num, error))
			return FALSE;
		if (sub_cfg_num == 0) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "sub_cfg_num is 0");
			return FALSE;
		}
		sub_cfg_info_pos = firmware_size + 12;
		cfg_offset = firmware_size + 6 + 64;
		for (guint i = 0; i < sub_cfg_num; i++) {
			guint8 sub_cfg_id = 0;
			guint16 sub_cfg_len = 0;
			if (!fu_memread_uint8_safe(buf,
						   bufsz,
						   sub_cfg_info_pos,
						   &sub_cfg_id,
						   error))
				return FALSE;
			if (!fu_memread_uint16_safe(buf,
						    bufsz,
						    sub_cfg_info_pos + 1,
						    &sub_cfg_len,
						    G_BIG_ENDIAN,
						    error))
				return FALSE;
			if (sensor_id == sub_cfg_id) {
				g_autoptr(GBytes) fw_img = NULL;
				g_autoptr(FuFirmware) img = fu_firmware_new();
				fu_firmware_set_idx(img, 3);
				fu_firmware_set_addr(img, 0x1E000);
				fw_img = fu_bytes_new_offset(fw, cfg_offset, sub_cfg_len, error);
				if (fw_img == NULL)
					return FALSE;
				fu_firmware_set_bytes(img, fw_img);
				fu_firmware_add_image(FU_FIRMWARE(self), img);
				if (!fu_memread_uint8_safe(buf, bufsz, cfg_offset, &cfg_ver, error))
					return FALSE;
				g_debug("Find a cfg match sensorID:ID=%d, cfg version=%d",
					sensor_id,
					cfg_ver);
				break;
			}
			cfg_offset += sub_cfg_len;
			sub_cfg_info_pos += 3;
		}
	}

	/* parse each image */
	subsys_num = fu_struct_goodix_gtx8_hdr_get_subsys_num(st);
	if (subsys_num == 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "subsys_num is 0, exit");
		return FALSE;
	}
	offset_hdr = st->len;
	for (guint i = 0; i < subsys_num; i++) {
		guint32 img_size;
		g_autoptr(GByteArray) st_img = NULL;

		st_img = fu_struct_goodix_gtx8_img_parse(buf, bufsz, offset_hdr, error);
		if (st_img == NULL)
			return FALSE;
		img_size = fu_struct_goodix_gtx8_img_get_size(st_img);
		if (fu_struct_goodix_gtx8_img_get_kind(st_img) != 0x01) {
			g_autoptr(GBytes) fw_img = NULL;
			g_autoptr(FuFirmware) img = fu_firmware_new();
			fu_firmware_set_idx(img, fu_struct_goodix_gtx8_img_get_kind(st_img));
			fu_firmware_set_addr(img, fu_struct_goodix_gtx8_img_get_addr(st_img) << 8);
			fw_img = fu_bytes_new_offset(fw, offset_payload, img_size, error);
			if (fw_img == NULL)
				return FALSE;
			fu_firmware_set_bytes(img, fw_img);
			fu_firmware_add_image(FU_FIRMWARE(self), img);
		}
		offset_hdr += st_img->len;
		offset_payload += img_size;
	}

	version = (fu_struct_goodix_gtx8_hdr_get_vid(st) << 8) | cfg_ver;
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

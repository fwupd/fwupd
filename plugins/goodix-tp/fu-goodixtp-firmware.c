/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-goodixtp-common.h"
#include "fu-goodixtp-firmware.h"

#define MAX_CHUNK_NUM 80

#define GTX8_FW_INFO_OFFSET 32
#define GTX8_FW_DATA_OFFSET 256

#define FW_HEADER_SIZE	      512
#define FW_SUBSYS_INFO_OFFSET 42
#define FW_SUBSYS_INFO_SIZE   10

struct goodix_chunk_info {
	guint8 type;
	guint32 flash_addr;
};

struct _FuGoodixtpFirmware {
	FuFirmwareClass parent_instance;
	guint8 patch_pid[9];
	guint8 patch_vid[4];
	guint8 cfg_ver;
	guint32 cfg_id;
	gint version;
	guint8 *fw_data;
	gint fw_len;
	gboolean has_config;
	gint index;
	struct goodix_chunk_info chunk_info[MAX_CHUNK_NUM];
};

G_DEFINE_TYPE(FuGoodixtpFirmware, fu_goodixtp_firmware, FU_TYPE_FIRMWARE)

gint
fu_goodixtp_firmware_get_version(FuGoodixtpFirmware *self)
{
	return self->version;
}

gboolean
fu_goodixtp_firmware_has_config(FuGoodixtpFirmware *self)
{
	return self->has_config;
}

guint8 *
fu_goodixtp_firmware_get_data(FuGoodixtpFirmware *self)
{
	return self->fw_data;
}

gint
fu_goodixtp_firmware_get_len(FuGoodixtpFirmware *self)
{
	return self->fw_len;
}

guint32
fu_goodixtp_firmware_get_addr(FuGoodixtpFirmware *self, gint index)
{
	return self->chunk_info[index].flash_addr;
}

static void
goodixtp_add_chunk_data(FuGoodixtpFirmware *self,
			guint8 type,
			guint32 addr,
			guint8 *data,
			gint dataLen)
{
	if (dataLen > RAM_BUFFER_SIZE)
		return;

	memcpy(self->fw_data + self->fw_len, data, dataLen);
	self->fw_len += RAM_BUFFER_SIZE;
	self->chunk_info[self->index].type = type;
	self->chunk_info[self->index].flash_addr = addr;
	self->index++;
}

static gboolean
goodixtp_brlb_firmware_parse(FuGoodixtpFirmware *self,
			     const guint8 *buf,
			     gint bufsz,
			     guint8 sensor_id,
			     GError **error)
{
	guint32 fw_offset = FW_HEADER_SIZE;
	guint32 info_offset = FW_SUBSYS_INFO_OFFSET;
	guint32 checksum = 0;
	guint8 *tmp_data;
	guint32 firmware_size;
	gint subsys_num;
	gint i;

	firmware_size = fu_memread_uint32(buf, G_LITTLE_ENDIAN) + 8;
	if ((gint)firmware_size < bufsz) {
		gint cfg_len = bufsz - (gint)firmware_size - 64;
		guint8 *cfg_data = (guint8 *)buf + firmware_size + 64;
		goodixtp_add_chunk_data(self, 4, 0x40000, cfg_data, cfg_len);
		self->cfg_ver = cfg_data[34];
		self->has_config = TRUE;
		g_debug("This bin file may contain config");
		g_debug("config size:%d, config ver:0x%02x", cfg_len, self->cfg_ver);
	}

	for (i = 8; i < (gint)firmware_size; i += 2)
		checksum += fu_memread_uint16(&buf[i], G_LITTLE_ENDIAN);

	if (checksum != fu_memread_uint32(&buf[4], G_LITTLE_ENDIAN)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_FILE,
			    "firmware bin checksum error");
		return FALSE;
	}

	subsys_num = buf[29];
	if (subsys_num == 0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INVALID_FILE, "subsys_num is 0, exit");
		return FALSE;
	}

	for (i = 0; i < subsys_num; i++) {
		guint8 tmp_type = buf[info_offset];
		guint32 tmp_size = fu_memread_uint32(&buf[info_offset + 1], G_LITTLE_ENDIAN);
		guint32 tmp_addr = fu_memread_uint32(&buf[info_offset + 5], G_LITTLE_ENDIAN);
		guint32 offset = 0;

		tmp_data = (guint8 *)buf + fw_offset;
		fw_offset += tmp_size;
		info_offset += 10;
		if (tmp_type == 0x0B || tmp_type == 0x01) {
			g_debug("skip type[%x] subsystem", tmp_type);
			continue;
		}

		for (gint j = 0; j < (gint)(tmp_size / RAM_BUFFER_SIZE); j++) {
			goodixtp_add_chunk_data(self,
						tmp_type,
						tmp_addr + offset,
						tmp_data + offset,
						RAM_BUFFER_SIZE);
			offset += RAM_BUFFER_SIZE;
		}
		if ((tmp_size % RAM_BUFFER_SIZE) != 0) {
			guint32 remain_len = tmp_size % RAM_BUFFER_SIZE;
			goodixtp_add_chunk_data(self,
						tmp_type,
						tmp_addr + offset,
						tmp_data + offset,
						remain_len);
		}
	}

	memcpy(self->patch_pid, &buf[17], 8);
	memcpy(self->patch_vid, &buf[25], 4);
	g_debug("Firmware PID:GT%s", self->patch_pid);
	g_debug("Firmware VID:%02x %02x %02x %02x",
		self->patch_vid[0],
		self->patch_vid[1],
		self->patch_vid[2],
		self->patch_vid[3]);
	self->version = (self->patch_vid[2] << 16) | (self->patch_vid[3] << 8) | self->cfg_ver;

	return TRUE;
}

static gboolean
goodixtp_gtx8_firmware_parse(FuGoodixtpFirmware *self,
			     const guint8 *buf,
			     gint bufsz,
			     guint8 sensor_id,
			     GError **error)
{
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
	guint32 fw_image_offset = GTX8_FW_DATA_OFFSET;

	firmware_size = fu_memread_uint32(buf, G_BIG_ENDIAN);
	if ((gint)firmware_size + 6 != bufsz) {
		g_debug("Check file len unequal %d != %d, this bin may contain config",
			(gint)firmware_size + 6,
			(gint)bufsz);
		self->has_config = TRUE;
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

	if (self->has_config) {
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
				goodixtp_add_chunk_data(self,
							3,
							0x1E000,
							(guint8 *)buf + cfg_offset,
							sub_cfg_len);
				self->cfg_ver = self->fw_data[0];
				g_debug("Find a cfg match sensorID:ID=%d, cfg version=%d",
					sensor_id,
					self->cfg_ver);
				break;
			}
			cfg_offset += sub_cfg_len;
			sub_cfg_info_pos += 3;
		}
		g_debug("sub_cfg_ver:0x%02x", self->cfg_ver);
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
			goodixtp_add_chunk_data(self,
						tmp_type,
						tmp_addr + offset,
						tmp_data + offset,
						RAM_BUFFER_SIZE);
			offset += RAM_BUFFER_SIZE;
		}
		if ((tmp_size % RAM_BUFFER_SIZE) != 0) {
			guint32 remain_len = tmp_size % RAM_BUFFER_SIZE;
			goodixtp_add_chunk_data(self,
						tmp_type,
						tmp_addr + offset,
						tmp_data + offset,
						remain_len);
		}
	}

	memcpy(self->patch_pid, &buf[15], 8);
	memcpy(self->patch_vid, &buf[23], 4);
	g_debug("Firmware PID:GT%s", self->patch_pid);
	g_debug("Firmware VID:%02x %02x %02x %02x",
		self->patch_vid[0],
		self->patch_vid[1],
		self->patch_vid[2],
		self->patch_vid[3]);
	self->version = (self->patch_vid[2] << 16) | (self->patch_vid[3] << 8) | self->cfg_ver;
	return TRUE;
}

gboolean
fu_goodixtp_frmware_parse(FuFirmware *firmware,
			  GBytes *fw,
			  gint ic_type,
			  guint8 sensor_id,
			  GError **error)
{
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	if (ic_type == IC_TYPE_NORMANDYL)
		return goodixtp_gtx8_firmware_parse(FU_GOODIXTP_FIRMWARE(firmware),
						    buf,
						    bufsz,
						    sensor_id,
						    error);
	if (ic_type == IC_TYPE_BERLINB)
		return goodixtp_brlb_firmware_parse(FU_GOODIXTP_FIRMWARE(firmware),
						    buf,
						    bufsz,
						    sensor_id,
						    error);

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "not support ic_type[%d] firmware",
		    ic_type);
	return FALSE;
}

static void
fu_goodixtp_firmware_init(FuGoodixtpFirmware *self)
{
	self->fw_data = g_malloc0(RAM_BUFFER_SIZE * MAX_CHUNK_NUM);
	self->index = 0;
	self->fw_len = 0;
	self->cfg_ver = 0;
}

static void
fu_goodixtp_firmware_class_init(FuGoodixtpFirmwareClass *klass)
{
}

FuFirmware *
fu_goodixtp_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_GOODIXTP_FIRMWARE, NULL));
}

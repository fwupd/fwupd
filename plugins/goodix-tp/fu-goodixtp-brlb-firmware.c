/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-goodixtp-brlb-firmware.h"
#include "fu-goodixtp-common.h"

struct _FuGoodixtpBrlbFirmware {
	FuGoodixtpFirmware parent_instance;
};

G_DEFINE_TYPE(FuGoodixtpBrlbFirmware, fu_goodixtp_brlb_firmware, FU_TYPE_GOODIXTP_FIRMWARE)

#define FW_HEADER_SIZE	      512
#define FW_SUBSYS_INFO_OFFSET 42
#define FW_SUBSYS_INFO_SIZE   10

gboolean
fu_goodixtp_brlb_firmware_parse(FuGoodixtpFirmware *self,
				GBytes *fw,
				guint8 sensor_id,
				GError **error)
{
	guint32 version;
	guint32 fw_offset = FW_HEADER_SIZE;
	guint32 info_offset = FW_SUBSYS_INFO_OFFSET;
	guint32 checksum = 0;
	guint8 *tmp_data;
	guint32 firmware_size;
	gint subsys_num;
	gint i;
	guint8 cfg_ver = 0;
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);

	if (!fu_memread_uint32_safe(buf, bufsz, 0, &firmware_size, G_LITTLE_ENDIAN, error))
		return FALSE;
	firmware_size += 8;
	if ((gsize)firmware_size < bufsz) {
		gint cfg_len = bufsz - (gint)firmware_size - 64;
		guint8 *cfg_data = (guint8 *)buf + firmware_size + 64;
		fu_goodixtp_add_chunk_data(self, 4, 0x40000, cfg_data, cfg_len);
		cfg_ver = cfg_data[34];
		g_debug("This bin file may contain config");
		g_debug("config size:%d, config ver:0x%02x", cfg_len, cfg_ver);
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

	version = (buf[27] << 16) | (buf[28] << 8) | cfg_ver;
	fu_goodixtp_firmware_set_version(self, version);

	return TRUE;
}

static void
fu_goodixtp_brlb_firmware_init(FuGoodixtpBrlbFirmware *self)
{
}

static void
fu_goodixtp_brlb_firmware_class_init(FuGoodixtpBrlbFirmwareClass *klass)
{
}

FuFirmware *
fu_goodixtp_brlb_firmware_new(void)
{
	return FU_FIRMWARE(g_object_new(FU_TYPE_GOODIXTP_BRLB_FIRMWARE, NULL));
}

/*
 * Copyright (C) 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-goodixtp-brlb-firmware.h"
#include "fu-goodixtp-common.h"
#include "fu-goodixtp-struct.h"

struct _FuGoodixtpBrlbFirmware {
	FuGoodixtpFirmware parent_instance;
};

G_DEFINE_TYPE(FuGoodixtpBrlbFirmware, fu_goodixtp_brlb_firmware, FU_TYPE_GOODIXTP_FIRMWARE)

#define FW_HEADER_SIZE 512

gboolean
fu_goodixtp_brlb_firmware_parse(FuGoodixtpFirmware *self,
				GBytes *fw,
				guint8 sensor_id,
				GError **error)
{
	guint32 version;
	gsize offset_hdr;
	gsize offset_payload = FW_HEADER_SIZE;
	guint32 checksum = 0;
	guint32 firmware_size;
	guint8 subsys_num;
	guint8 cfg_ver = 0;
	gsize bufsz = 0;
	const guint8 *buf = g_bytes_get_data(fw, &bufsz);
	g_autoptr(GByteArray) st = NULL;

	st = fu_struct_goodix_brlb_hdr_parse(buf, bufsz, 0x0, error);
	if (st == NULL)
		return FALSE;
	firmware_size = fu_struct_goodix_brlb_hdr_get_firmware_size(st);
	firmware_size += 8;

	/* [payload][64 bytes padding?][config] */
	if ((gsize)firmware_size < bufsz) {
		g_autoptr(GBytes) fw_img = NULL;
		g_autoptr(FuFirmware) img = fu_firmware_new();
		fu_firmware_set_idx(img, 4);
		fu_firmware_set_addr(img, 0x40000);
		fw_img = fu_bytes_new_offset(fw,
					     firmware_size + 64,
					     bufsz - (gsize)firmware_size - 64,
					     error);
		if (fw_img == NULL)
			return FALSE;
		fu_firmware_set_bytes(img, fw_img);
		fu_firmware_add_image(FU_FIRMWARE(self), img);
		if (!fu_memread_uint8_safe(buf, bufsz, firmware_size + 64 + 34, &cfg_ver, error))
			return FALSE;
		g_debug("config size:0x%x, config ver:0x%02x",
			(guint)fu_firmware_get_size(img),
			cfg_ver);
	}

	/* verify checksum */
	for (guint i = 8; i < firmware_size; i += 2) {
		guint16 tmp_val;
		if (!fu_memread_uint16_safe(buf, bufsz, i, &tmp_val, G_LITTLE_ENDIAN, error))
			return FALSE;
		checksum += tmp_val;
	}
	if (checksum != fu_struct_goodix_brlb_hdr_get_checksum(st)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid checksum");
		return FALSE;
	}

	/* parse each image */
	subsys_num = fu_struct_goodix_brlb_hdr_get_subsys_num(st);
	if (subsys_num == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid subsys_num");
		return FALSE;
	}
	offset_hdr = st->len;
	for (guint i = 0; i < subsys_num; i++) {
		guint32 img_size;
		g_autoptr(GByteArray) st_img = NULL;

		st_img = fu_struct_goodix_brlb_img_parse(buf, bufsz, offset_hdr, error);
		if (st_img == NULL)
			return FALSE;

		img_size = fu_struct_goodix_brlb_img_get_size(st_img);
		if (fu_struct_goodix_brlb_img_get_kind(st_img) != 0x0B &&
		    fu_struct_goodix_brlb_img_get_kind(st_img) != 0x01) {
			g_autoptr(GBytes) fw_img = NULL;
			g_autoptr(FuFirmware) img = fu_firmware_new();
			fu_firmware_set_idx(img, fu_struct_goodix_brlb_img_get_kind(st_img));
			fu_firmware_set_addr(img, fu_struct_goodix_brlb_img_get_addr(st_img));
			fw_img = fu_bytes_new_offset(fw, offset_payload, img_size, error);
			if (fw_img == NULL)
				return FALSE;
			fu_firmware_set_bytes(img, fw_img);
			fu_firmware_add_image(FU_FIRMWARE(self), img);
		}
		offset_hdr += st_img->len;
		offset_payload += img_size;
	}

	version = (fu_struct_goodix_brlb_hdr_get_vid(st) << 8) | cfg_ver;
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

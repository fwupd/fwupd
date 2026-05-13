/*
 * Copyright 2023 Goodix.inc <xulinkun@goodix.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-goodixtp-brlb-config.h"
#include "fu-goodixtp-brlb-firmware.h"
#include "fu-goodixtp-common.h"
#include "fu-goodixtp-struct.h"

struct _FuGoodixtpBrlbFirmware {
	FuGoodixtpFirmware parent_instance;
};

G_DEFINE_TYPE(FuGoodixtpBrlbFirmware, fu_goodixtp_brlb_firmware, FU_TYPE_GOODIXTP_FIRMWARE)

#define FW_HEADER_SIZE 512

static gboolean
fu_goodixtp_brlb_firmware_parse_config(FuGoodixtpFirmware *self,
				       GInputStream *stream,
				       gsize offset,
				       guint8 sensor_id,
				       guint8 *cfg_ver,
				       guint8 *update_flag,
				       GError **error)
{
	gboolean found_cfg = FALSE;
	gsize cfg_offset = 0;
	gsize streamsz = 0;
	guint cfg_len;
	guint16 cfg_checksum = 0;
	g_autoptr(FuFirmware) img = fu_goodixtp_brlb_config_new();
	g_autoptr(FuStructGoodixTpCfgGroup) st_cfg = NULL;
	g_autoptr(GInputStream) stream_img = NULL;

	st_cfg = fu_struct_goodix_tp_cfg_group_parse_stream(stream, offset, error);
	if (st_cfg == NULL)
		return FALSE;

	/* verify checksum */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	for (guint i = offset + 6; i < streamsz; i++) {
		guint8 tmp_val = 0;
		if (!fu_input_stream_read_u8(stream, i, &tmp_val, error))
			return FALSE;
		cfg_checksum += tmp_val;
	}
	if (cfg_checksum != fu_struct_goodix_tp_cfg_group_get_checksum(st_cfg)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid config group checksum");
		return FALSE;
	}

	*update_flag = fu_struct_goodix_tp_cfg_group_get_update_flag(st_cfg);

	/* find a config item for this sensor */
	for (guint i = 0; i < fu_struct_goodix_tp_cfg_group_get_cfg_num(st_cfg); i++) {
		g_autoptr(FuStructGoodixTpCfgItem) st_cfg_item = NULL;

		st_cfg_item = fu_struct_goodix_tp_cfg_item_parse_stream(stream,
									offset + 6 + (gsize)i * 3,
									error);
		if (st_cfg_item == NULL)
			return FALSE;
		cfg_len = fu_struct_goodix_tp_cfg_item_get_len(st_cfg_item);
		if (fu_struct_goodix_tp_cfg_item_get_id(st_cfg_item) == sensor_id) {
			found_cfg = TRUE;
			break;
		}
		if (!fu_size_checked_inc(&cfg_offset, cfg_len, error))
			return FALSE;
	}
	if (!found_cfg) {
		g_debug("no config found for sensor_id=0x%02x, skip config update", sensor_id);
		return TRUE;
	}

	/* found! */
	g_debug("found config for sensor_id=0x%02x, cfg_len=0x%x", sensor_id, cfg_len);
	stream_img = fu_partial_input_stream_new(stream, offset + 64 + cfg_offset, cfg_len, error);
	if (stream_img == NULL)
		return FALSE;
	if (!fu_firmware_parse_stream(img,
				      stream_img,
				      0x0,
				      FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM,
				      error))
		return FALSE;
	if (!fu_firmware_add_image(FU_FIRMWARE(self), img, error))
		return FALSE;
	*cfg_ver = (guint8)fu_firmware_get_version_raw(img);
	g_debug("config size:0x%x, config ver:0x%02x",
		(guint)fu_firmware_get_size(img),
		(guint)fu_firmware_get_version_raw(img));

	/* success */
	return TRUE;
}

gboolean
fu_goodixtp_brlb_firmware_parse(FuGoodixtpFirmware *self,
				GInputStream *stream,
				guint8 sensor_id,
				GError **error)
{
	gsize firmware_size;
	gsize offset_hdr;
	gsize offset_payload = FW_HEADER_SIZE;
	gsize streamsz = 0;
	guint32 checksum = 0;
	guint32 flash_addr;
	guint32 version_raw;
	guint8 cfg_ver = 0;
	guint8 inter_ver;
	guint8 subsys_num;
	guint8 type;
	guint8 update_flag = 0xFF;
	guint8 vice_ver;
	g_autoptr(FuStructGoodixBrlbHdr) st = NULL;

	st = fu_struct_goodix_brlb_hdr_parse_stream(stream, 0x0, error);
	if (st == NULL)
		return FALSE;
	firmware_size = fu_struct_goodix_brlb_hdr_get_firmware_size(st);
	if (!fu_size_checked_inc(&firmware_size, 8, error))
		return FALSE;

	/* verify checksum */
	for (guint i = 8; i < firmware_size; i += 2) {
		guint16 tmp_val = 0;
		if (!fu_input_stream_read_u16(stream, i, &tmp_val, G_LITTLE_ENDIAN, error))
			return FALSE;
		checksum += tmp_val;
	}
	if (checksum != fu_struct_goodix_brlb_hdr_get_checksum(st)) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_FILE,
				    "invalid firmware checksum");
		return FALSE;
	}

	/* [firmware][64 bytes padding][cfg1][cfg2][cfg3]... */
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (firmware_size < streamsz) {
		if (!fu_goodixtp_brlb_firmware_parse_config(self,
							    stream,
							    firmware_size,
							    sensor_id,
							    &cfg_ver,
							    &update_flag,
							    error))
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
	offset_hdr = st->buf->len;
	for (guint i = 0; i < subsys_num; i++) {
		guint32 img_size;
		g_autoptr(FuFirmware) img = fu_firmware_new();
		g_autoptr(FuStructGoodixBrlbImg) st_img = NULL;
		g_autoptr(GInputStream) stream_img = NULL;

		st_img = fu_struct_goodix_brlb_img_parse_stream(stream, offset_hdr, error);
		if (st_img == NULL)
			return FALSE;

		img_size = fu_struct_goodix_brlb_img_get_size(st_img);
		type = fu_struct_goodix_brlb_img_get_kind(st_img);
		flash_addr = fu_struct_goodix_brlb_img_get_addr(st_img);
		if (type == 0x01) {
			/* bootloader, not need to write to flash */
			if (!fu_size_checked_inc(&offset_hdr, st_img->buf->len, error))
				return FALSE;
			if (!fu_size_checked_inc(&offset_payload, img_size, error))
				return FALSE;
			continue;
		}
		if (type == 0x0B && !(update_flag & 0x80)) {
			/* hidsubsystem, not need to write to flash when update_flag bit7 is 0 */
			if (!fu_size_checked_inc(&offset_hdr, st_img->buf->len, error))
				return FALSE;
			if (!fu_size_checked_inc(&offset_payload, img_size, error))
				return FALSE;
			continue;
		}

		fu_firmware_set_idx(img, type);
		fu_firmware_set_addr(img, flash_addr);
		stream_img = fu_partial_input_stream_new(stream, offset_payload, img_size, error);
		if (stream_img == NULL)
			return FALSE;
		if (!fu_firmware_parse_stream(img,
					      stream_img,
					      0x0,
					      FU_FIRMWARE_PARSE_FLAG_CACHE_STREAM,
					      error))
			return FALSE;
		if (!fu_firmware_add_image(FU_FIRMWARE(self), img, error))
			return FALSE;
		if (!fu_size_checked_inc(&offset_hdr, st_img->buf->len, error))
			return FALSE;
		if (!fu_size_checked_inc(&offset_payload, img_size, error))
			return FALSE;
	}

	vice_ver = (fu_struct_goodix_brlb_hdr_get_vid(st) >> 8) & 0xFF;
	inter_ver = fu_struct_goodix_brlb_hdr_get_vid(st) & 0xFF;
	version_raw = (vice_ver << 16) | (inter_ver << 8) | cfg_ver;
	fu_firmware_set_version_raw(FU_FIRMWARE(self), version_raw);
	return TRUE;
}

static void
fu_goodixtp_brlb_firmware_init(FuGoodixtpBrlbFirmware *self)
{
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_FIRMWARE);
	fu_firmware_add_image_gtype(FU_FIRMWARE(self), FU_TYPE_GOODIXTP_BRLB_CONFIG);
	fu_firmware_set_images_max(FU_FIRMWARE(self), 1024);
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

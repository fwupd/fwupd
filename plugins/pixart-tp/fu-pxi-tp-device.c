/*
 * Copyright 2025 Harris Tai <harris_tai@pixart.com>
 * Copyright 2025 Micky Hsieh <micky_hsieh@pixart.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/*
 * NOTE: DO NOT ALLOW ANY MORE MAGIC CONSTANTS IN THIS FILE
 * nocheck:magic-inlines=100
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "fu-pxi-tp-common.h"
#include "fu-pxi-tp-device.h"
#include "fu-pxi-tp-firmware.h"
#include "fu-pxi-tp-register.h"
#include "fu-pxi-tp-struct.h"
#include "fu-pxi-tp-tf-communication.h"

struct _FuPxiTpDevice {
	FuHidrawDevice parent_instance;
	guint8 sram_select;
	guint8 ver_bank;
	guint16 ver_addr;
};

G_DEFINE_TYPE(FuPxiTpDevice, fu_pxi_tp_device, FU_TYPE_HIDRAW_DEVICE)

static gboolean
fu_pxi_tp_device_flash_execute(FuDevice *device,
			       guint8 inst_cmd,
			       guint32 ccr_cmd,
			       guint16 data_cnt,
			       GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	guint8 out_val;

	WRITE_REG(0x04, 0x2c, inst_cmd);

	WRITE_REG(0x04, 0x40, (ccr_cmd >> 0) & 0xff);
	WRITE_REG(0x04, 0x41, (ccr_cmd >> 8) & 0xff);
	WRITE_REG(0x04, 0x42, (ccr_cmd >> 16) & 0xff);
	WRITE_REG(0x04, 0x43, (ccr_cmd >> 24) & 0xff);

	WRITE_REG(0x04, 0x44, (data_cnt >> 0) & 0xff);
	WRITE_REG(0x04, 0x45, (data_cnt >> 8) & 0xff);

	WRITE_REG(0x04, 0x56, 0x01);

	for (guint i = 0; i < 10; i++) {
		fu_device_sleep(device, 1);
		READ_REG(0x04, 0x56, &out_val);
		if (out_val == 0)
			break;
	}

	if (out_val != 0) {
		fu_pxi_tp_common_fail(error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_WRITE,
				      "Flash executes failure.");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_write_enable(FuDevice *device, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);
	guint8 out_val;

	if (!fu_pxi_tp_device_flash_execute(device, 0x00, 0x00000106, 0, error))
		return FALSE;

	for (guint i = 0; i < 10; i++) {
		if (!fu_pxi_tp_device_flash_execute(device, 0x01, 0x01000105, 1, error))
			return FALSE;
		fu_device_sleep(device, 1);
		READ_REG(0x04, 0x1c, &out_val);
		if ((out_val & 0x02) == 0x02)
			break;
	}

	if ((out_val & 0x02) != 0x02) {
		fu_pxi_tp_common_fail(error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_WRITE,
				      "Flash write enable failure.");
		g_debug("Flash write enable failure.");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_wait_busy(FuDevice *device, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	guint8 out_val;

	for (guint i = 0; i < 1000; i++) {
		if (!fu_pxi_tp_device_flash_execute(device, 0x01, 0x01000105, 1, error))
			return FALSE;
		fu_device_sleep(device, 1);
		READ_REG(0x04, 0x1c, &out_val);
		if ((out_val & 0x01) == 0x00)
			break;
	}

	if ((out_val & 0x01) != 0x00) {
		fu_pxi_tp_common_fail(error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_WRITE,
				      "Flash wait busy failure.");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_erase_sector(FuDevice *device, guint8 sector, GError **error)
{
	guint32 flash_address = (guint32)(sector) * 4096;
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	if (!fu_pxi_tp_device_flash_wait_busy(device, error))
		return FALSE;

	if (!fu_pxi_tp_device_flash_write_enable(device, error))
		return FALSE;

	WRITE_REG(0x04, 0x48, (flash_address >> 0) & 0xff);
	WRITE_REG(0x04, 0x49, (flash_address >> 8) & 0xff);
	WRITE_REG(0x04, 0x4a, (flash_address >> 16) & 0xff);
	WRITE_REG(0x04, 0x4b, (flash_address >> 24) & 0xff);

	if (!fu_pxi_tp_device_flash_execute(device, 0x00, 0x00002520, 0, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_pxi_tp_device_flash_program_256b_to_flash(FuDevice *device,
					     guint8 sector,
					     guint8 page,
					     GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	guint32 flash_address = (guint32)(sector) * 4096 + (guint32)(page) * 256;

	if (!fu_pxi_tp_device_flash_wait_busy(device, error))
		return FALSE;

	if (!fu_pxi_tp_device_flash_write_enable(device, error))
		return FALSE;

	WRITE_REG(0x04, 0x2e, 0x00);
	WRITE_REG(0x04, 0x2f, 0x00);

	WRITE_REG(0x04, 0x48, (flash_address >> 0) & 0xff);
	WRITE_REG(0x04, 0x49, (flash_address >> 8) & 0xff);
	WRITE_REG(0x04, 0x4a, (flash_address >> 16) & 0xff);
	WRITE_REG(0x04, 0x4b, (flash_address >> 24) & 0xff);

	if (!fu_pxi_tp_device_flash_execute(device, 0x84, 0x01002502, 256, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_pxi_tp_device_write_sram_256b(FuDevice *device, const guint8 *data, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	WRITE_REG(0x06, 0x10, 0x00);
	WRITE_REG(0x06, 0x11, 0x00);

	WRITE_REG(0x06, 0x09, self->sram_select);

	WRITE_REG(0x06, 0x0a, 0x00);
	if (!fu_pxi_tp_register_burst_write(self, data, 256, error)) {
		fu_pxi_tp_common_fail(error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_WRITE,
				      "Burst write buffer failure.");
		g_debug("Burst write buffer failure.");
		return FALSE;
	}
	WRITE_REG(0x06, 0x0a, 0x01);

	return TRUE;
}

static gboolean
fu_pxi_tp_device_reset(FuDevice *device, guint8 key1, guint8 key2, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	WRITE_REG(0x01, 0x2c, key1);
	fu_device_sleep(device, 30);
	WRITE_REG(0x01, 0x2d, key2);

	if (key2 == 0xbb) {
		fu_device_sleep(device, 500);
	} else {
		fu_device_sleep(device, 10);
	}

	return TRUE;
}

static gboolean
fu_pxi_tp_device_firmware_clear(FuDevice *device, FuPxiTpFirmware *ctn, GError **error)
{
	guint32 start_address;
	if (ctn == NULL) {
		fu_pxi_tp_common_fail(error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_WRITE,
				      "firmware container is NULL");
		return FALSE;
	}

	start_address = fu_pxi_tp_firmware_get_firmware_address(ctn);

	if (!fu_pxi_tp_device_flash_erase_sector(device, (start_address / 4096), error)) {
		fu_pxi_tp_common_fail(error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_WRITE,
				      "Clear firmware failure.");
		return FALSE;
	}
	return TRUE;
}

static guint32
fu_pxi_tp_device_crc_firmware(FuDevice *device, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	guint8 out_val = 0;
	guint8 swap_flag = 0;
	guint16 part_id = 0;
	guint32 return_value = 0;

	READ_REG(0x04, 0x29, &out_val);
	swap_flag = out_val;
	READ_REG(0x00, 0x78, &out_val);
	part_id = out_val;
	READ_REG(0x00, 0x79, &out_val);
	part_id += out_val << 8;

	switch (part_id) {
	case 0x0274:
		if (swap_flag) {
			fu_pxi_tp_register_user_write(self, 0x00, 0x82, 0x10, error);
		} else {
			fu_pxi_tp_register_user_write(self, 0x00, 0x82, 0x02, error);
		}
		break;
	default:
		fu_pxi_tp_register_user_write(self, 0x00, 0x82, 0x02, error);
		break;
	}

	for (guint i = 0; i < 1000; i++) {
		fu_device_sleep(device, 10);
		fu_pxi_tp_register_user_read(self, 0x00, 0x82, &out_val, error);
		if ((out_val & 0x01) == 0x00)
			break;
	}

	fu_pxi_tp_register_user_read(self, 0x00, 0x84, &out_val, error);
	return_value += out_val;
	fu_pxi_tp_register_user_read(self, 0x00, 0x85, &out_val, error);
	return_value += out_val << 8;
	fu_pxi_tp_register_user_read(self, 0x00, 0x86, &out_val, error);
	return_value += out_val << 16;
	fu_pxi_tp_register_user_read(self, 0x00, 0x87, &out_val, error);
	return_value += out_val << 24;

	g_debug("Firmware CRC: 0x%08x", (guint)return_value);

	return return_value;
}

static guint32
fu_pxi_tp_device_crc_parameter(FuDevice *device, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	guint8 out_val = 0;
	guint8 swap_flag = 0;
	guint16 part_id = 0;
	guint32 return_value = 0;

	READ_REG(0x04, 0x29, &out_val);
	swap_flag = out_val;
	READ_REG(0x00, 0x78, &out_val);
	part_id = out_val;
	READ_REG(0x00, 0x79, &out_val);
	part_id += out_val << 8;

	switch (part_id) {
	case 0x0274:
		if (swap_flag) {
			fu_pxi_tp_register_user_write(self, 0x00, 0x82, 0x20, error);
		} else {
			fu_pxi_tp_register_user_write(self, 0x00, 0x82, 0x04, error);
		}
		break;
	default:
		fu_pxi_tp_register_user_write(self, 0x00, 0x82, 0x04, error);
		break;
	}

	for (guint i = 0; i < 1000; i++) {
		fu_device_sleep(device, 10);
		fu_pxi_tp_register_user_read(self, 0x00, 0x82, &out_val, error);
		if ((out_val & 0x01) == 0x00)
			break;
	}

	fu_pxi_tp_register_user_read(self, 0x00, 0x84, &out_val, error);
	return_value += out_val;
	fu_pxi_tp_register_user_read(self, 0x00, 0x85, &out_val, error);
	return_value += out_val << 8;
	fu_pxi_tp_register_user_read(self, 0x00, 0x86, &out_val, error);
	return_value += out_val << 16;
	fu_pxi_tp_register_user_read(self, 0x00, 0x87, &out_val, error);
	return_value += out_val << 24;

	g_debug("Parameter CRC: 0x%08x", (guint)return_value);

	return return_value;
}

static gboolean
fu_pxi_tp_device_update_flash_process(FuDevice *device,
				      FuProgress *progress,
				      guint32 data_size,
				      guint8 start_sector,
				      GByteArray *data,
				      GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	FuProgress *update_progress = NULL;
	guint8 sector_cnt = 0;
	guint8 page_cnt = 0;
	guint16 offset = 0;
	guint8 max_sector_cnt = (data_size >> 12) + (((data_size & 0x00000fff) == 0) ? 0 : 1);

	WRITE_REG(0x02, 0x0d, 0x02);

	update_progress = fu_progress_get_child(progress);
	fu_progress_set_id(update_progress, G_STRLOC);
	fu_progress_add_flag(update_progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_set_steps(update_progress, max_sector_cnt * 2);

	for (sector_cnt = 0; sector_cnt < max_sector_cnt; sector_cnt++) {
		if (!fu_pxi_tp_device_flash_erase_sector(device,
							 start_sector + sector_cnt,
							 error)) {
			fu_pxi_tp_common_fail(error,
					      FWUPD_ERROR,
					      FWUPD_ERROR_WRITE,
					      "Burst write buffer failure.");
			return FALSE;
		}

		fu_progress_step_done(update_progress);
	}

	for (sector_cnt = 0; sector_cnt < max_sector_cnt; sector_cnt++) {
		for (page_cnt = 1; page_cnt < 16; page_cnt++) {
			offset = (sector_cnt * 4096) + (page_cnt * 256);

			g_autoptr(GByteArray) buf = g_byte_array_new();
			gsize remain = data->len > offset ? data->len - offset : 0;
			gsize copy_len = remain < 256 ? remain : 256;

			if (copy_len == 0)
				break;

			g_byte_array_append(buf, &data->data[offset], copy_len);

			/* pad to 256 with 0xFF if needed */
			if (copy_len < 256) {
				guint8 pad[256] = {0};
				memset(pad, 0xFF, sizeof(pad));
				g_byte_array_append(buf, pad, 256 - copy_len);
			}

			if (!fu_pxi_tp_device_write_sram_256b(device, buf->data, error)) {
				fu_pxi_tp_common_fail(error,
						      FWUPD_ERROR,
						      FWUPD_ERROR_WRITE,
						      "Write SRAM failure.");
				g_debug("Error: %s", (*error)->message);
				return FALSE;
			}
			if (!fu_pxi_tp_device_flash_program_256b_to_flash(device,
									  start_sector + sector_cnt,
									  page_cnt,
									  error)) {
				fu_pxi_tp_common_fail(error,
						      FWUPD_ERROR,
						      FWUPD_ERROR_WRITE,
						      "Flash program failure.");
				g_debug("Error: %s", (*error)->message);
				return FALSE;
			}
		}

		offset = sector_cnt * 4096;

		g_autoptr(GByteArray) buf = g_byte_array_new();
		gsize remain = data->len > offset ? data->len - offset : 0;
		gsize copy_len = remain < 256 ? remain : 256;

		if (copy_len == 0) {
			fu_progress_step_done(update_progress);
			break;
		}

		g_byte_array_append(buf, &data->data[offset], copy_len);

		/* pad to 256 with 0xFF if needed */
		if (copy_len < 256) {
			guint8 pad[256] = {0};
			memset(pad, 0xFF, sizeof(pad));
			g_byte_array_append(buf, pad, 256 - copy_len);
		}

		if (!fu_pxi_tp_device_write_sram_256b(device, buf->data, error)) {
			fu_pxi_tp_common_fail(error,
					      FWUPD_ERROR,
					      FWUPD_ERROR_WRITE,
					      "Write SRAM failure.");
			g_debug("Error: %s", (*error)->message);
			return FALSE;
		}
		if (!fu_pxi_tp_device_flash_program_256b_to_flash(device,
								  start_sector + sector_cnt,
								  0,
								  error)) {
			fu_pxi_tp_common_fail(error,
					      FWUPD_ERROR,
					      FWUPD_ERROR_WRITE,
					      "Flash program failure.");
			g_debug("Error: %s", (*error)->message);
			return FALSE;
		}

		fu_progress_step_done(update_progress);
	}

	return TRUE;
}

static void
fu_pxi_tp_device_to_string(FuDevice *device, guint idt, GString *str)
{
	/* To-do */
}

static gboolean
fu_pxi_tp_device_probe(FuDevice *device, GError **error)
{
	return TRUE;
}

static gboolean
fu_pxi_tp_device_setup(FuDevice *device, GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	guint8 lo = 0, hi = 0;
	guint16 ver_u16 = 0;
	READ_USR_REG(self->ver_bank, self->ver_addr + 0, &lo);
	READ_USR_REG(self->ver_bank, self->ver_addr + 1, &hi);

	ver_u16 = (guint16)lo | ((guint16)hi << 8); /* low byte first */

	g_debug("PXI-TP setup: read version bytes: lo=0x%02x hi=0x%02x (LE) -> ver=0x%04x",
		(guint)lo,
		(guint)hi,
		(guint)ver_u16);

	g_autofree gchar *ver_str = g_strdup_printf("0x%04x", ver_u16);
	fu_device_set_version(device, ver_str);
	return TRUE;
}

static FuPxiTpFirmware *
fu_pxi_tp_device_wrap_or_parse_ctn(FuFirmware *maybe_generic, GError **error)
{
	if (FU_IS_PXI_TP_FIRMWARE(maybe_generic))
		return FU_PXI_TP_FIRMWARE(maybe_generic);

	g_autoptr(GBytes) bytes = fu_firmware_get_bytes_with_patches(maybe_generic, error);
	if (bytes == NULL)
		return NULL;

	g_autoptr(GInputStream) mis = g_memory_input_stream_new_from_bytes(bytes);
	FuFirmware *ctn = FU_FIRMWARE(g_object_new(FU_TYPE_PXI_TP_FIRMWARE, NULL));
	if (!fu_firmware_parse_stream(ctn, mis, 0, FU_FIRMWARE_PARSE_FLAG_NONE, error)) {
		fu_pxi_tp_common_fail(error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_INVALID_FILE,
				      "pxi-tp parse failed: ");
		g_object_unref(ctn);
		return NULL;
	}
	return FU_PXI_TP_FIRMWARE(ctn);
}

static gboolean
fu_pxi_tp_device_write_firmware(FuDevice *device,
				FuFirmware *firmware,
				FuProgress *progress,
				FwupdInstallFlags flags,
				GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	FuPxiTpFirmware *ctn = NULL;
	const GPtrArray *secs = NULL;
	FuProgress *prog_write = NULL;
	FuProgress *prog_verify = NULL;
	guint64 total_bytes = 0;
	guint64 written = 0;
	guint32 crc_value = 0;

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 2, NULL);

	ctn = fu_pxi_tp_device_wrap_or_parse_ctn(firmware, error);
	if (ctn == NULL)
		return FALSE;

	secs = fu_pxi_tp_firmware_get_sections(ctn);
	if (secs == NULL || secs->len == 0) {
		fu_pxi_tp_common_fail(error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_INVALID_FILE,
				      "no sections to write");
		return FALSE;
	}

	for (guint i = 0; i < secs->len; i++) {
		FuPxiTpSection *s = g_ptr_array_index((GPtrArray *)secs, i);
		if (s->is_valid_update && !s->is_external && s->section_length > 0)
			total_bytes += (guint64)s->section_length;
	}

	if (total_bytes == 0) {
		fu_pxi_tp_common_fail(error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_INVALID_FILE,
				      "no internal/valid sections to write");
		return FALSE;
	}

	prog_write = fu_progress_get_child(progress);
	fu_progress_set_id(prog_write, G_STRLOC);
	fu_progress_set_steps(prog_write, secs->len);

	/* 清除舊 firmware（最佳努力，不檢查回傳值） */
	fu_pxi_tp_device_firmware_clear(device, ctn, error);

	for (guint i = 0; i < secs->len; i++) {
		guint8 start_sector;
		FuPxiTpSection *s = g_ptr_array_index((GPtrArray *)secs, i);

		if (!s->is_valid_update || s->is_external || s->section_length == 0) {
			fu_progress_step_done(prog_write);
			continue;
		}

		g_autoptr(GByteArray) data =
		    fu_pxi_tp_firmware_get_slice_by_file(ctn,
							 (gsize)s->internal_file_start,
							 (gsize)s->section_length,
							 error);
		if (data == NULL)
			return FALSE;

		start_sector = (guint8)(s->target_flash_start / 4096);

		g_debug("PXI-TP: write section %u: flash=0x%08x, file_off=0x%08" G_GINT64_MODIFIER
			"x, len=%u, sector=%u, data_len=%u",
			i,
			s->target_flash_start,
			(gint64)s->internal_file_start,
			(guint)s->section_length,
			start_sector,
			(guint)data->len);

		if (data->len == 0) {
			fu_pxi_tp_common_fail(error,
					      FWUPD_ERROR,
					      FWUPD_ERROR_INVALID_FILE,
					      "empty payload for section %u",
					      i);
			return FALSE;
		}

		switch (s->update_type) {
		case PXI_TP_UPDATE_TYPE_GENERAL:
		case PXI_TP_UPDATE_TYPE_FW_SECTION:
		case PXI_TP_UPDATE_TYPE_PARAM:
			if (!fu_pxi_tp_device_update_flash_process(self,
								   prog_write,
								   (guint)s->section_length,
								   start_sector,
								   data,
								   error)) {
				fu_pxi_tp_common_fail(error,
						      FWUPD_ERROR,
						      FWUPD_ERROR_WRITE,
						      "write section failed.");
				return FALSE;
			}
			break;

		case PXI_TP_UPDATE_TYPE_TF_FORCE: {
			/* target TF version is stored in s->reserved[0..2] */
			guint8 target_ver[3] = {
			    s->reserved[0],
			    s->reserved[1],
			    s->reserved[2],
			};

			fu_pxi_tp_device_reset(device, 0xaa, 0xbb, error);
			guint32 send_interval = (guint32)s->reserved[3]; /* ms */
			g_debug("TF: send interval (ms): %u", send_interval);
			g_debug("PXI-TP: update TF firmware, section %u, len=%u",
				i,
				(guint)data->len);

			if (!fu_pxi_tp_tf_communication_write_firmware_process(device,
									       prog_write,
									       send_interval,
									       (guint32)data->len,
									       data,
									       target_ver,
									       error)) {
				/* 注意：內部已經會設好 error，這裡不能再呼叫 fu_pxi_tp_common_fail
				 */
				return FALSE;
			}

			fu_pxi_tp_device_reset(device, 0xaa, 0xcc, error);
			break;
		}

		default:
			fu_pxi_tp_common_fail(error,
					      FWUPD_ERROR,
					      FWUPD_ERROR_INVALID_FILE,
					      "not support update type for section %u",
					      i);
			return FALSE;
		}

		fu_progress_step_done(prog_write);
	}

	fu_progress_step_done(progress);

	prog_verify = fu_progress_get_child(progress);
	(void)flags;
	fu_progress_set_id(prog_verify, G_STRLOC);
	fu_progress_set_steps(prog_verify, 2);

	/* CRC Check */
	if (!fu_pxi_tp_device_reset(device, 0xaa, 0xcc, error))
		return FALSE;

	crc_value = fu_pxi_tp_device_crc_firmware(device, error);
	if (crc_value != fu_pxi_tp_firmware_get_file_firmware_crc(ctn)) {
		fu_pxi_tp_common_fail(error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_INVALID_FILE,
				      "Firmware CRC compare failed");
		fu_pxi_tp_device_firmware_clear(device, ctn, NULL);
		return FALSE;
	}

	fu_progress_step_done(prog_verify);

	crc_value = fu_pxi_tp_device_crc_parameter(device, error);
	if (crc_value != fu_pxi_tp_firmware_get_file_parameter_crc(ctn)) {
		fu_pxi_tp_common_fail(error,
				      FWUPD_ERROR,
				      FWUPD_ERROR_INVALID_FILE,
				      "Parameter CRC compare failed");
		fu_pxi_tp_device_firmware_clear(device, ctn, error);
		return FALSE;
	}

	fu_progress_step_done(prog_verify);

	fu_progress_step_done(progress);
	g_debug("update success.");
	return TRUE;
}

static gboolean
fu_pxi_tp_device_set_quirk_kv(FuDevice *device,
			      const gchar *key,
			      const gchar *value,
			      GError **error)
{
	FuPxiTpDevice *self = FU_PXI_TP_DEVICE(device);

	if (g_strcmp0(key, "HidVersionReg") == 0) {
		GStrv parts;
		guint i;

		/* allow: "bank=0x00; addr=0x0b" (fixed 2 bytes, LE) */
		parts = g_strsplit(value, ";", -1);

		for (i = 0; parts && parts[i]; i++) {
			gchar *kv;
			GStrv kvp = NULL;
			const gchar *k = NULL;
			const gchar *v = NULL;
			guint64 tmp = 0;

			kv = g_strstrip(parts[i]);
			if (*kv == '\0')
				continue;

			kvp = g_strsplit(kv, "=", 2);
			if (!kvp[0] || !kvp[1]) {
				g_strfreev(kvp);
				continue;
			}

			k = g_strstrip(kvp[0]);
			v = g_strstrip(kvp[1]);

			if (g_ascii_strcasecmp(k, "bank") == 0) {
				if (!fu_strtoull(v, &tmp, 0, 0xff, FU_INTEGER_BASE_AUTO, error)) {
					g_strfreev(kvp);
					g_strfreev(parts);
					return FALSE;
				}
				self->ver_bank = (guint8)tmp;

			} else if (g_ascii_strcasecmp(k, "addr") == 0) {
				if (!fu_strtoull(v, &tmp, 0, 0xffff, FU_INTEGER_BASE_AUTO, error)) {
					g_strfreev(kvp);
					g_strfreev(parts);
					return FALSE;
				}
				self->ver_addr = (guint16)tmp;
			}

			g_strfreev(kvp);
		}

		g_strfreev(parts);

		g_debug("quirk: HidVersionReg parsed => bank=0x%02x addr=0x%04x",
			(guint)self->ver_bank,
			(guint)self->ver_addr);
		return TRUE;
	}

	if (g_strcmp0(key, "SramSelect") == 0) {
		guint64 tmp = 0;
		if (!fu_strtoull(value, &tmp, 0, 0xff, FU_INTEGER_BASE_AUTO, error))
			return FALSE;
		self->sram_select = (guint8)tmp;
		g_debug("quirk: SramSelect parsed => 0x%02x", (guint)self->sram_select);
		return TRUE;
	}

	/* Error handle for unknown quirk */
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_SUPPORTED,
		    "quirk key not supported: %s",
		    key);
	return FALSE;
}

static void
fu_pxi_tp_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 57, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 43, "reload");
}

static void
fu_pxi_tp_device_init(FuPxiTpDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_HEX);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.pixart.tp");
	fu_device_set_summary(FU_DEVICE(self), "Touchpad");
	fu_device_add_icon(FU_DEVICE(self), "input-touchpad");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);

	/* Quirk default value */
	self->sram_select = 0x0f;
	self->ver_bank = 0x00;
	self->ver_addr = 0x0b;
}

static gboolean
fu_pxi_tp_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	if (!fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_pxi_tp_device_setup(device, error))
			g_warning("Failed to refresh version (already attached)");
		return TRUE;
	}

	if (!fu_pxi_tp_device_reset(device, 0xaa, 0xbb, error)) {
		return FALSE;
	}

	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	g_debug("Exit Bootloader");

	if (!fu_pxi_tp_device_setup(device, error))
		g_warning("Failed to refresh version after attached");

	return TRUE;
}

static gboolean
fu_pxi_tp_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER))
		return TRUE;

	if (!fu_pxi_tp_device_reset(device, 0xaa, 0xcc, error)) {
		return FALSE;
	}

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	g_debug("Enter Bootloader");
	return TRUE;
}

static gboolean
fu_pxi_tp_device_cleanup(FuDevice *device,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	g_debug("fu_pxi_tp_device_cleanup");
	if (fu_device_has_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER)) {
		if (!fu_pxi_tp_device_reset(device, 0xaa, 0xbb, error)) {
			return FALSE;
		}
		fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		g_debug("Exit Bootloader");
	}

	return TRUE; /* Cleanup should avoid reporting errors if possible */
}

static void
fu_pxi_tp_device_class_init(FuPxiTpDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->probe = fu_pxi_tp_device_probe;
	klass_device->setup = fu_pxi_tp_device_setup;
	klass_device->write_firmware = fu_pxi_tp_device_write_firmware;
	klass_device->attach = fu_pxi_tp_device_attach;
	klass_device->detach = fu_pxi_tp_device_detach;
	klass_device->cleanup = fu_pxi_tp_device_cleanup;
	klass_device->set_progress = fu_pxi_tp_device_set_progress;
	klass_device->set_quirk_kv = fu_pxi_tp_device_set_quirk_kv;
	klass_device->to_string = fu_pxi_tp_device_to_string;
}

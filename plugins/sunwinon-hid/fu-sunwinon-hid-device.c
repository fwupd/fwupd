/*
 * Copyright 2019 GOODIX
 * Copyright 2026 Sunwinon Electronics Co., Ltd.
 *
 * These are derived from GOODIX's dfu_master.c and dfu_master.h files, which are part of the
 * GOODIX GR551x SDK available here: https://github.com/goodix-ble/GR551x.SDK
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR BSD-3-Clause
 */

#include "config.h"

#include "fu-sunwinon-hid-device.h"
#include "fu-sunwinon-hid-firmware.h"
#include "fu-sunwinon-hid-struct.h"

#define FU_SUNWINON_HID_DEVICE_REBOOT_WAIT_TIME 2000 /* ms */

struct _FuSunwinonHidDevice {
	FuHidrawDevice parent_instance;
	FuSunwinonFwType fw_type;
	guint32 dfu_save_addr;
	guint32 bin_size;
	guint32 load_addr;
};

G_DEFINE_TYPE(FuSunwinonHidDevice, fu_sunwinon_hid_device, FU_TYPE_HIDRAW_DEVICE)

#define FU_SUNWINON_DFU_CONFIG_PERIPHERAL_FLASH_START_ADDR 0x200000

#define FU_SUNWINON_HID_REPORT_DATA_LEN	  480
#define FU_SUNWINON_HID_DFU_SIGN_LEN	  856
#define FU_SUNWINON_HID_DEVICE_PACKET_LEN 464

static void
fu_sunwinon_hid_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuSunwinonHidDevice *self = FU_SUNWINON_HID_DEVICE(device);
	fwupd_codec_string_append(str, idt, "FwType", fu_sunwinon_fw_type_to_string(self->fw_type));
	fwupd_codec_string_append_hex(str, idt, "DfuSaveAddr", self->dfu_save_addr);
	fwupd_codec_string_append_hex(str, idt, "BinSize", self->bin_size);
	fwupd_codec_string_append_hex(str, idt, "LoadAddr", self->load_addr);
}

static gboolean
fu_sunwinon_hid_device_dfu_pre_update_check(FuSunwinonHidDevice *self,
					    FuSunwinonHidFirmware *firmware_sh,
					    GError **error)
{
	gsize tail_size = FU_STRUCT_SUNWINON_DFU_IMAGE_INFO_SIZE;
	guint32 bootloader_end = 0;

	/* check if fw is signed */
	if (self->fw_type == FU_SUNWINON_FW_TYPE_SIGNED ||
	    fu_sunwinon_hid_firmware_get_fw_type(firmware_sh) == FU_SUNWINON_FW_TYPE_SIGNED) {
		tail_size += FU_SUNWINON_HID_DFU_SIGN_LEN;
	}

	/* check if the new fw would overlap with bootloader */
	bootloader_end = self->load_addr + self->bin_size + tail_size;
	if (self->dfu_save_addr <= bootloader_end) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "firmware save address 0x%x overlaps with bootloader "
			    "(bootloader_end: 0x%x)",
			    self->dfu_save_addr,
			    bootloader_end);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_sunwinon_hid_device_dfu_send_frame(FuSunwinonHidDevice *self,
				      const guint8 *buf,
				      guint16 bufsz,
				      FuSunwinonDfuCmd cmd_type,
				      GError **error)
{
	gsize total_len = (3 * 2) + bufsz + 2; /* +2 for checksum */
	guint16 checksum;
	g_autoptr(FuStructSunwinonHidOut) st = fu_struct_sunwinon_hid_out_new();

	/* sanity check */
	if (total_len > FU_SUNWINON_HID_REPORT_DATA_LEN) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "data length exceeds maximum report size");
		return FALSE;
	}

	fu_struct_sunwinon_hid_out_set_data_len(st, total_len);
	fu_struct_sunwinon_hid_out_set_dfu_cmd_type(st, cmd_type);
	fu_struct_sunwinon_hid_out_set_dfu_data_len(st, bufsz);
	if (buf != NULL) {
		if (!fu_struct_sunwinon_hid_out_set_data(st, buf, bufsz, error))
			return FALSE;
	}

	/* write checksum at the very end of the whole data package */
	checksum =
	    fu_sum16(st->buf->data + FU_STRUCT_SUNWINON_HID_OUT_OFFSET_DFU_CMD_TYPE, bufsz + 4);
	if (!fu_memwrite_uint16_safe(st->buf->data,
				     st->buf->len,
				     FU_STRUCT_SUNWINON_HID_OUT_OFFSET_DATA + bufsz,
				     checksum,
				     G_LITTLE_ENDIAN,
				     error))
		return FALSE;
	return fu_hidraw_device_set_report(FU_HIDRAW_DEVICE(self),
					   st->buf->data,
					   st->buf->len,
					   FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
					   error);
}

static GByteArray *
fu_sunwinon_hid_device_dfu_recv_frame(FuSunwinonHidDevice *self,
				      FuSunwinonDfuCmd cmd_expected,
				      GError **error)
{
	FuSunwinonDfuCmd cmd_actual;
	guint16 checksum_actual = 0;
	guint16 checksum_calc;
	guint8 buf[FU_STRUCT_SUNWINON_HID_IN_SIZE] = {0};
	g_autoptr(FuStructSunwinonHidIn) st = NULL;
	g_autoptr(GByteArray) bufout = g_byte_array_new();
	g_autoptr(GError) error_local = NULL;

	/* may not get a full length report here */
	if (!fu_hidraw_device_get_report(FU_HIDRAW_DEVICE(self),
					 buf,
					 sizeof(buf),
					 FU_IO_CHANNEL_FLAG_SINGLE_SHOT,
					 &error_local)) {
		if (!g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_READ)) {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return NULL;
		}
	}
	fu_dump_raw(G_LOG_DOMAIN, "raw input report", buf, sizeof(buf));

	/* check command */
	st = fu_struct_sunwinon_hid_in_parse(buf, sizeof(buf), 0, error);
	if (st == NULL)
		return NULL;
	cmd_actual = fu_struct_sunwinon_hid_in_get_dfu_cmd_type(st);
	if (cmd_actual != cmd_expected) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "unexpected command type in response, expected %s, got %s",
			    fu_sunwinon_dfu_cmd_to_string(cmd_expected),
			    fu_sunwinon_dfu_cmd_to_string(cmd_actual));
		return NULL;
	}
	if (!fu_byte_array_append_safe(bufout,
				       st->buf->data,
				       st->buf->len,
				       FU_STRUCT_SUNWINON_HID_IN_OFFSET_DATA,
				       fu_struct_sunwinon_hid_in_get_dfu_data_len(st),
				       error))
		return NULL;

	/* checksum from the cmd-type to the end of the data section */
	if (!fu_memread_uint16_safe(st->buf->data,
				    st->buf->len,
				    FU_STRUCT_SUNWINON_HID_IN_OFFSET_DATA + bufout->len,
				    &checksum_actual,
				    G_LITTLE_ENDIAN,
				    error))
		return NULL;
	checksum_calc =
	    fu_sum16(buf + FU_STRUCT_SUNWINON_HID_IN_OFFSET_DFU_CMD_TYPE, 4 + bufout->len);
	if (checksum_calc != checksum_actual) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "received frame checksum mismatch");
		return NULL;
	}

	/* success */
	return g_steal_pointer(&bufout);
}

static gboolean
fu_sunwinon_hid_device_dfu_plain_ack_recv(FuSunwinonHidDevice *self,
					  FuSunwinonDfuCmd cmd_expected,
					  GError **error)
{
	guint8 ack_byte = 0;
	g_autoptr(GByteArray) buf = NULL;

	buf = fu_sunwinon_hid_device_dfu_recv_frame(self, cmd_expected, error);
	if (buf == NULL)
		return FALSE;
	if (!fu_memread_uint8_safe(buf->data, buf->len, 0x0, &ack_byte, error))
		return FALSE;
	if (ack_byte != FU_SUNWINON_DFU_ACK_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "command %s not acked successfully",
			    fu_sunwinon_dfu_cmd_to_string(cmd_expected));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_sunwinon_hid_device_dfu_get_info_cmd(FuSunwinonHidDevice *self, GError **error)
{
	g_autoptr(FuStructSunwinonDfuRspGetInfo) st = NULL;
	g_autoptr(GByteArray) buf = NULL;

	if (!fu_sunwinon_hid_device_dfu_send_frame(self,
						   NULL,
						   0,
						   FU_SUNWINON_DFU_CMD_GET_INFO,
						   error))
		return FALSE;
	buf = fu_sunwinon_hid_device_dfu_recv_frame(self, FU_SUNWINON_DFU_CMD_GET_INFO, error);
	if (buf == NULL)
		return FALSE;
	st = fu_struct_sunwinon_dfu_rsp_get_info_parse(buf->data, buf->len, 0, error);
	if (st == NULL)
		return FALSE;
	if (fu_struct_sunwinon_dfu_rsp_get_info_get_ack_status(st) != FU_SUNWINON_DFU_ACK_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "command %s not acked successfully",
			    fu_sunwinon_dfu_cmd_to_string(FU_SUNWINON_DFU_CMD_GET_INFO));
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_sunwinon_hid_device_dfu_system_info_cmd(FuSunwinonHidDevice *self, GError **error)
{
	g_autoptr(FuStructSunwinonDfuPayloadSystemInfo) st_payload = NULL;
	g_autoptr(FuStructSunwinonDfuRspSystemInfo) st_info = NULL;
	g_autoptr(GByteArray) buf = NULL;

	st_payload = fu_struct_sunwinon_dfu_payload_system_info_new();
	fu_struct_sunwinon_dfu_payload_system_info_set_flash_start_addr(
	    st_payload,
	    FU_SUNWINON_DFU_CONFIG_PERIPHERAL_FLASH_START_ADDR);
	if (!fu_sunwinon_hid_device_dfu_send_frame(self,
						   st_payload->buf->data,
						   st_payload->buf->len,
						   FU_SUNWINON_DFU_CMD_SYSTEM_INFO,
						   error))
		return FALSE;

	buf = fu_sunwinon_hid_device_dfu_recv_frame(self, FU_SUNWINON_DFU_CMD_SYSTEM_INFO, error);
	if (buf == NULL)
		return FALSE;
	st_info = fu_struct_sunwinon_dfu_rsp_system_info_parse(buf->data, buf->len, 0, error);
	if (st_info == NULL)
		return FALSE;
	if (fu_struct_sunwinon_dfu_rsp_system_info_get_ack_status(st_info) !=
	    FU_SUNWINON_DFU_ACK_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "command %s not acked successfully",
			    fu_sunwinon_dfu_cmd_to_string(FU_SUNWINON_DFU_CMD_SYSTEM_INFO));
		return FALSE;
	}
	if (fu_struct_sunwinon_dfu_rsp_system_info_get_start_addr(st_info) !=
	    FU_SUNWINON_DFU_CONFIG_PERIPHERAL_FLASH_START_ADDR) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "peripheral flash start address mismatch");
		return FALSE;
	}

	self->bin_size = fu_struct_sunwinon_dfu_rsp_system_info_get_bin_size(st_info);
	self->load_addr = fu_struct_sunwinon_dfu_rsp_system_info_get_load_addr(st_info);
	if ((fu_struct_sunwinon_dfu_rsp_system_info_get_opcode(st_info) & 0xF0) != 0)
		self->fw_type = FU_SUNWINON_FW_TYPE_SIGNED;

	/* success */
	return TRUE;
}

static gboolean
fu_sunwinon_hid_device_dfu_fw_info_ensure(FuSunwinonHidDevice *self, GError **error)
{
	g_autoptr(FuStructSunwinonDfuRspFwInfoGet) st_fw = NULL;
	g_autoptr(FuStructSunwinonDfuImageInfo) st_info = NULL;
	g_autoptr(GByteArray) buf = NULL;

	if (!fu_sunwinon_hid_device_dfu_send_frame(self,
						   NULL,
						   0,
						   FU_SUNWINON_DFU_CMD_FW_INFO_GET,
						   error))
		return FALSE;
	buf = fu_sunwinon_hid_device_dfu_recv_frame(self, FU_SUNWINON_DFU_CMD_FW_INFO_GET, error);
	if (buf == NULL)
		return FALSE;
	st_fw = fu_struct_sunwinon_dfu_rsp_fw_info_get_parse(buf->data, buf->len, 0, error);
	if (st_fw == NULL)
		return FALSE;
	if (fu_struct_sunwinon_dfu_rsp_fw_info_get_get_ack_status(st_fw) !=
	    FU_SUNWINON_DFU_ACK_SUCCESS) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "command %s not acked successfully",
			    fu_sunwinon_dfu_cmd_to_string(FU_SUNWINON_DFU_CMD_FW_INFO_GET));
		return FALSE;
	}
	self->dfu_save_addr = fu_struct_sunwinon_dfu_rsp_fw_info_get_get_dfu_save_addr(st_fw);

	/* set version */
	st_info = fu_struct_sunwinon_dfu_rsp_fw_info_get_get_image_info(st_fw);
	fu_device_set_version_raw(FU_DEVICE(self),
				  fu_struct_sunwinon_dfu_image_info_get_version(st_info));

	/* success */
	return TRUE;
}

static gboolean
fu_sunwinon_hid_device_dfu_mode_set_cmd(FuSunwinonHidDevice *self,
					FuSunwinonDfuUpgradeMode copy_mode,
					GError **error)
{
	if (!fu_sunwinon_hid_device_dfu_send_frame(self,
						   (const guint8 *)&copy_mode,
						   1,
						   FU_SUNWINON_DFU_CMD_MODE_SET,
						   error))
		return FALSE;

	/* has no response; wait a while for device getting ready */
	fu_device_sleep(FU_DEVICE(self), 100);
	return TRUE;
}

static gboolean
fu_sunwinon_hid_device_dfu_program_start_cmd(FuSunwinonHidDevice *self,
					     FuSunwinonHidFirmware *firmware_sh,
					     GError **error)
{
	gsize streamsz = 0;
	g_autoptr(FuStructSunwinonDfuPayloadProgramStart) st_prog_start =
	    fu_struct_sunwinon_dfu_payload_program_start_new();
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuStructSunwinonDfuImageInfo) st_info = NULL;

	stream = fu_firmware_get_stream(FU_FIRMWARE(firmware_sh), error);
	if (stream == NULL)
		return FALSE;
	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	st_info = fu_struct_sunwinon_dfu_image_info_parse_stream(
	    stream,
	    streamsz - FU_STRUCT_SUNWINON_DFU_IMAGE_INFO_SIZE,
	    error);
	if (st_info == NULL)
		return FALSE;
	fu_struct_sunwinon_dfu_image_info_set_load_addr(st_info, self->dfu_save_addr);

	g_byte_array_set_size(st_info->buf, st_info->buf->len - 8);
	if (!fu_struct_sunwinon_dfu_payload_program_start_set_image_info_raw(st_prog_start,
									     st_info->buf->data,
									     st_info->buf->len,
									     error))
		return FALSE;
	fu_struct_sunwinon_dfu_payload_program_start_set_mode(st_prog_start, self->fw_type);

	/* send+recv */
	if (!fu_sunwinon_hid_device_dfu_send_frame(self,
						   st_prog_start->buf->data,
						   st_prog_start->buf->len,
						   FU_SUNWINON_DFU_CMD_PROGRAM_START,
						   error))
		return FALSE;
	if (!fu_sunwinon_hid_device_dfu_plain_ack_recv(self,
						       FU_SUNWINON_DFU_CMD_PROGRAM_START,
						       error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_sunwinon_hid_device_dfu_do_update_normal(FuSunwinonHidDevice *self,
					    FuSunwinonHidFirmware *firmware_sh,
					    guint32 *file_checksum, /* out */
					    FuProgress *progress,
					    GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* chunkify */
	stream = fu_firmware_get_stream(FU_FIRMWARE(firmware_sh), error);
	if (stream == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_stream(stream,
						0x0,
						0x0,
						FU_SUNWINON_HID_DEVICE_PACKET_LEN,
						error);
	if (chunks == NULL)
		return FALSE;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_set_steps(progress, fu_chunk_array_length(chunks));
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		const guint8 *buf;
		guint16 data_len = 0;
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(FuStructSunwinonDfuPayloadProgramFlash) st_flash =
		    fu_struct_sunwinon_dfu_payload_program_flash_new();

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		fu_struct_sunwinon_dfu_payload_program_flash_set_dfu_save_addr(
		    st_flash,
		    self->dfu_save_addr + fu_chunk_get_address(chk));

		buf = fu_chunk_get_data(chk);
		data_len = fu_chunk_get_data_sz(chk);
		fu_struct_sunwinon_dfu_payload_program_flash_set_data_len(st_flash, data_len);
		if (!fu_struct_sunwinon_dfu_payload_program_flash_set_fw_data(st_flash,
									      buf,
									      data_len,
									      error))
			return FALSE;

		if (!fu_sunwinon_hid_device_dfu_send_frame(
			self,
			st_flash->buf->data,
			st_flash->buf->len - (FU_SUNWINON_HID_DEVICE_PACKET_LEN - data_len),
			FU_SUNWINON_DFU_CMD_PROGRAM_FLASH,
			error))
			return FALSE;
		if (!fu_sunwinon_hid_device_dfu_plain_ack_recv(self,
							       FU_SUNWINON_DFU_CMD_PROGRAM_FLASH,
							       error))
			return FALSE;

		/* update file checksum */
		*file_checksum += fu_sum32(buf, data_len);
		fu_progress_step_done(progress);
	}

	/* verify checksum */
	if (*file_checksum != fu_sunwinon_hid_firmware_get_full_checksum(firmware_sh)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "file checksum mismatch, expected %u, got %u",
			    fu_sunwinon_hid_firmware_get_full_checksum(firmware_sh),
			    *file_checksum);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_sunwinon_hid_device_dfu_program_end_cmd_normal(FuSunwinonHidDevice *self,
						  guint32 file_checksum,
						  GError **error)
{
	g_autoptr(FuStructSunwinonDfuPayloadProgramEnd) st_end =
	    fu_struct_sunwinon_dfu_payload_program_end_new();

	/* send+recv */
	fu_struct_sunwinon_dfu_payload_program_end_set_file_checksum(st_end, file_checksum);
	if (!fu_sunwinon_hid_device_dfu_send_frame(self,
						   st_end->buf->data,
						   st_end->buf->len,
						   FU_SUNWINON_DFU_CMD_PROGRAM_END,
						   error))
		return FALSE;
	if (!fu_sunwinon_hid_device_dfu_plain_ack_recv(self,
						       FU_SUNWINON_DFU_CMD_PROGRAM_END,
						       error))
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_sunwinon_hid_device_write_firmware(FuDevice *device,
				      FuFirmware *firmware,
				      FuProgress *progress,
				      FwupdInstallFlags flags,
				      GError **error)
{
	FuSunwinonHidDevice *self = FU_SUNWINON_HID_DEVICE(device);
	FuSunwinonHidFirmware *firmware_sh = FU_SUNWINON_HID_FIRMWARE(firmware);
	guint32 file_checksum = 0;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "start");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");

	/* for now, all buf are unsigned unencrypted images */
	if (!fu_sunwinon_hid_device_dfu_get_info_cmd(self, error))
		return FALSE;
	if (!fu_sunwinon_hid_device_dfu_system_info_cmd(self, error))
		return FALSE;
	if (!fu_sunwinon_hid_device_dfu_fw_info_ensure(self, error))
		return FALSE;
	if (!fu_sunwinon_hid_device_dfu_pre_update_check(self, firmware_sh, error))
		return FALSE;
	if (!fu_sunwinon_hid_device_dfu_mode_set_cmd(self,
						     FU_SUNWINON_DFU_UPGRADE_MODE_COPY,
						     error))
		return FALSE;
	if (!fu_sunwinon_hid_device_dfu_program_start_cmd(self, firmware_sh, error))
		return FALSE;
	fu_progress_step_done(progress);

	/* send chunks */
	if (!fu_sunwinon_hid_device_dfu_do_update_normal(self,
							 firmware_sh,
							 &file_checksum,
							 fu_progress_get_child(progress),
							 error))
		return FALSE;
	fu_progress_step_done(progress);

	/* done */
	if (!fu_sunwinon_hid_device_dfu_program_end_cmd_normal(self,
							       file_checksum,
							       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_sunwinon_hid_device_setup(FuDevice *device, GError **error)
{
	FuSunwinonHidDevice *self = FU_SUNWINON_HID_DEVICE(device);
	g_autoptr(FuHidDescriptor) descriptor = NULL;
	g_autoptr(FuHidReport) report_out = NULL;
	g_autoptr(FuHidReport) report_in = NULL;

	descriptor = fu_hidraw_device_parse_descriptor(FU_HIDRAW_DEVICE(device), error);
	if (descriptor == NULL)
		return FALSE;
	report_out = fu_hid_descriptor_find_report(descriptor,
						   error,
						   "report-id",
						   FU_SUNWINON_HID_REPORT_CHANNEL_ID,
						   "usage",
						   0x01,
						   "output",
						   0x02,
						   NULL);
	if (report_out == NULL)
		return FALSE;
	report_in = fu_hid_descriptor_find_report(descriptor,
						  error,
						  "report-id",
						  FU_SUNWINON_HID_REPORT_CHANNEL_ID,
						  "usage",
						  0x01,
						  "input",
						  0x02,
						  NULL);
	if (report_in == NULL)
		return FALSE;

	g_debug("wait for service ready");
	fu_device_sleep(device, FU_SUNWINON_HID_DEVICE_REBOOT_WAIT_TIME);
	if (!fu_sunwinon_hid_device_dfu_fw_info_ensure(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_sunwinon_hid_device_init(FuSunwinonHidDevice *self)
{
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_TABLET);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_protocol(FU_DEVICE(self), "com.sunwinon.hid");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_SUNWINON_HID_FIRMWARE);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
}

static void
fu_sunwinon_hid_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-firmware");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gchar *
fu_sunwinon_hid_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return g_strdup_printf("%u.%u.%u",
			       (guint)((version_raw >> 12) & 0x0F),
			       (guint)((version_raw >> 8) & 0x0F),
			       (guint)(version_raw & 0xFF));
}

static void
fu_sunwinon_hid_device_class_init(FuSunwinonHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_sunwinon_hid_device_to_string;
	device_class->setup = fu_sunwinon_hid_device_setup;
	device_class->write_firmware = fu_sunwinon_hid_device_write_firmware;
	device_class->set_progress = fu_sunwinon_hid_device_set_progress;
	device_class->convert_version = fu_sunwinon_hid_device_convert_version;
}

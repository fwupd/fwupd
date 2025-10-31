/*
 * Copyright 2025 lazro <2059899519@qq.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#include "fu-legion-hid-device.h"
#include "fu-legion-hid-firmware.h"
#include "fu-legion-hid-struct.h"

struct _FuLegionHidDevice {
	FuHidrawDevice parent_instance;
};

G_DEFINE_TYPE(FuLegionHidDevice, fu_legion_hid_device, FU_TYPE_HIDRAW_DEVICE)

static gboolean
fu_legion_hid_device_read_normal_response_retry_cb(FuDevice *self,
						   gpointer user_data,
						   GError **error)
{
	FuLegionHidNormalRetryHelper *helper = (FuLegionHidNormalRetryHelper *)user_data;
	GByteArray *res = helper->res;
	guint8 main_id = helper->main_id;
	guint8 sub_id = helper->sub_id;
	guint8 dev_id = helper->dev_id;

	g_byte_array_set_size(res, FU_LEGION_HID_DEVICE_FW_REPORT_LENGTH);
	if (!fu_udev_device_read(FU_UDEV_DEVICE(self),
				 res->data,
				 res->len,
				 NULL,
				 FU_LEGION_HID_DEVICE_IO_TIMEOUT,
				 FU_IO_CHANNEL_FLAG_NONE,
				 error))
		return FALSE;

	if (res->data[2] == main_id && res->data[3] == sub_id && res->data[4] == dev_id)
		return TRUE;

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_BUSY,
		    "response mismatch filter(%u, %u, %u), retrying...",
		    main_id,
		    sub_id,
		    dev_id);
	return FALSE;
}

static gboolean
fu_legion_hid_device_read_response(FuLegionHidDevice *self,
				   FuLegionHidNormalRetryHelper *helper,
				   GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_legion_hid_device_read_normal_response_retry_cb,
				    10,
				    0,
				    (gpointer)helper,
				    error);
}

static gboolean
fu_legion_hid_device_read_upgrade_response_retry_cb(FuDevice *self,
						    gpointer user_data,
						    GError **error)
{
	FuLegionHidUpgradeRetryHelper *helper = (FuLegionHidUpgradeRetryHelper *)user_data;
	GByteArray *res = helper->res;
	guint8 main_id = helper->main_id;
	guint8 sub_id = helper->sub_id;
	guint8 dev_id = helper->dev_id;
	guint8 step = helper->step;
	gboolean is_valid_dev_id = FALSE;

	g_byte_array_set_size(res, FU_LEGION_HID_DEVICE_FW_REPORT_LENGTH);
	if (!fu_udev_device_read(FU_UDEV_DEVICE(self),
				 res->data,
				 res->len,
				 NULL,
				 FU_LEGION_HID_DEVICE_IO_TIMEOUT,
				 FU_IO_CHANNEL_FLAG_NONE,
				 error))
		return FALSE;

	is_valid_dev_id = (res->data[4] == dev_id) ||
			  (res->data[4] == FU_LEGION_HID_DEVICE_ID_GAMEPAD_L &&
			   (dev_id == FU_LEGION_HID_DEVICE_ID_GAMEPAD_L2 ||
			    dev_id == FU_LEGION_HID_DEVICE_ID_GAMEPAD_L3)) ||
			  (res->data[4] == FU_LEGION_HID_DEVICE_ID_GAMEPAD_R &&
			   (dev_id == FU_LEGION_HID_DEVICE_ID_GAMEPAD_R2 ||
			    dev_id == FU_LEGION_HID_DEVICE_ID_GAMEPAD_R3));

	if (res->data[2] == main_id && res->data[3] == sub_id && is_valid_dev_id &&
	    res->data[7] == step && res->data[9] != FU_LEGION_HID_RESPONSE_STATUS_BUSY)
		return TRUE;

	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_BUSY,
		    "response mismatch filter(%u, %u, %u, %u), retrying...",
		    main_id,
		    sub_id,
		    dev_id,
		    step);
	return FALSE;
}

static gboolean
fu_legion_hid_device_read_upgrade_response(FuLegionHidDevice *self,
					   FuLegionHidUpgradeRetryHelper *helper,
					   GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_legion_hid_device_read_upgrade_response_retry_cb,
				    120,
				    0,
				    (gpointer)helper,
				    error);
}

static gboolean
fu_legion_hid_device_upgrade_start(FuLegionHidDevice *self,
				   guint8 id,
				   guint16 crc16,
				   guint size,
				   GError **error)
{
	g_autoptr(GByteArray) res = NULL;
	g_autoptr(FuStructLegionHidUpgradeCmd) st_cmd = fu_struct_legion_hid_upgrade_cmd_new();
	g_autoptr(FuStructLegionHidUpgradeStartParam) st_content =
	    fu_struct_legion_hid_upgrade_start_param_new();
	FuLegionHidUpgradeRetryHelper helper = {0};
	guint8 status = 0;

	fu_struct_legion_hid_upgrade_start_param_set_crc16(st_content, crc16);
	fu_struct_legion_hid_upgrade_start_param_set_size(st_content, size);
	fu_struct_legion_hid_upgrade_cmd_set_length(st_cmd, st_content->buf->len + 5);
	fu_struct_legion_hid_upgrade_cmd_set_device_id(st_cmd, id);
	if (!fu_struct_legion_hid_upgrade_cmd_set_data(st_cmd,
						       st_content->buf->data,
						       st_content->buf->len,
						       error))
		return FALSE;
	if (!fu_udev_device_write(FU_UDEV_DEVICE(self),
				  st_cmd->buf->data,
				  st_cmd->buf->len,
				  FU_LEGION_HID_DEVICE_IO_TIMEOUT,
				  FU_IO_CHANNEL_FLAG_NONE,
				  error))
		return FALSE;

	res = g_byte_array_sized_new(FU_LEGION_HID_DEVICE_FW_REPORT_LENGTH);
	helper.res = res;
	helper.main_id = fu_struct_legion_hid_upgrade_cmd_get_main_id(st_cmd);
	helper.sub_id = fu_struct_legion_hid_upgrade_cmd_get_sub_id(st_cmd);
	helper.dev_id = id;
	helper.step = FU_LEGION_HID_UPGRADE_STEP_START;
	if (!fu_legion_hid_device_read_upgrade_response(self, &helper, error))
		return FALSE;
	status = res->data[9];
	if (status != FU_LEGION_HID_RESPONSE_STATUS_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "device report start command failed with %u",
			    status);
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_legion_hid_device_upgrade_query_size(FuLegionHidDevice *self,
					guint8 id,
					guint *max_size,
					GError **error)
{
	g_autoptr(GByteArray) res = NULL;
	g_autoptr(FuStructLegionHidUpgradeCmd) st_cmd = fu_struct_legion_hid_upgrade_cmd_new();
	FuLegionHidUpgradeRetryHelper helper = {0};
	guint8 status = 0;
	guint8 content[] = {0x02,
			    FU_LEGION_HID_UPGRADE_STEP_QUERY_SIZE,
			    FU_LEGION_HID_CMD_CONSTANT_SN};

	fu_struct_legion_hid_upgrade_cmd_set_length(st_cmd, sizeof(content) + 5);
	fu_struct_legion_hid_upgrade_cmd_set_device_id(st_cmd, id);
	if (!fu_struct_legion_hid_upgrade_cmd_set_data(st_cmd, content, sizeof(content), error))
		return FALSE;
	if (!fu_udev_device_write(FU_UDEV_DEVICE(self),
				  st_cmd->buf->data,
				  st_cmd->buf->len,
				  FU_LEGION_HID_DEVICE_IO_TIMEOUT,
				  FU_IO_CHANNEL_FLAG_NONE,
				  error))
		return FALSE;

	res = g_byte_array_sized_new(FU_LEGION_HID_DEVICE_FW_REPORT_LENGTH);
	helper.res = res;
	helper.main_id = fu_struct_legion_hid_upgrade_cmd_get_main_id(st_cmd);
	helper.sub_id = fu_struct_legion_hid_upgrade_cmd_get_sub_id(st_cmd);
	helper.dev_id = id;
	helper.step = FU_LEGION_HID_UPGRADE_STEP_QUERY_SIZE;
	if (!fu_legion_hid_device_read_upgrade_response(self, &helper, error))
		return FALSE;
	status = res->data[9];
	if (status == FU_LEGION_HID_RESPONSE_STATUS_FAIL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "device report query size command failed with %u",
			    status);
		return FALSE;
	}
	if (max_size)
		*max_size = fu_memread_uint16(res->data + 9, G_BIG_ENDIAN);

	return TRUE;
}

static gboolean
fu_legion_hid_device_upgrade_write_data_chunks(FuLegionHidDevice *self,
					       guint8 id,
					       guint max_size,
					       FuChunkArray *chunks,
					       GError **error)
{
	guint send_size = 0;
	guint ready_send_size = 0;
	guint recieved_size = 0;
	g_autoptr(FuStructLegionHidUpgradeCmd) st_cmd = fu_struct_legion_hid_upgrade_cmd_new();
	g_autoptr(FuStructLegionHidUpgradePacket) st_packet =
	    fu_struct_legion_hid_upgrade_packet_new();
	g_autoptr(GByteArray) res = g_byte_array_sized_new(FU_LEGION_HID_DEVICE_FW_REPORT_LENGTH);
	FuLegionHidUpgradeRetryHelper helper = {0};
	gboolean wait = FALSE;

	for (guint i = 0; i < fu_chunk_array_length(chunks); ++i) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		if (!fu_struct_legion_hid_upgrade_packet_set_data(st_packet,
								  fu_chunk_get_data(chk),
								  fu_chunk_get_data_sz(chk),
								  error))
			return FALSE;
		fu_struct_legion_hid_upgrade_cmd_set_length(st_cmd, st_packet->buf->len + 5);
		fu_struct_legion_hid_upgrade_cmd_set_device_id(st_cmd, id);
		fu_struct_legion_hid_upgrade_cmd_set_param(
		    st_cmd,
		    FU_LEGION_HID_CMD_CONSTANT_UPGRADE_SEND_DATA);
		if (!fu_struct_legion_hid_upgrade_cmd_set_data(st_cmd,
							       st_packet->buf->data,
							       st_packet->buf->len,
							       error))
			return FALSE;
		ready_send_size = send_size + fu_chunk_get_data_sz(chk);
		wait = (ready_send_size % max_size == 0);
		if (!fu_udev_device_write(FU_UDEV_DEVICE(self),
					  st_cmd->buf->data,
					  st_cmd->buf->len,
					  FU_LEGION_HID_DEVICE_IO_TIMEOUT,
					  FU_IO_CHANNEL_FLAG_NONE,
					  error))
			return FALSE;
		send_size = ready_send_size;
		if (wait) {
			helper.res = res;
			helper.main_id = fu_struct_legion_hid_upgrade_cmd_get_main_id(st_cmd);
			helper.sub_id = fu_struct_legion_hid_upgrade_cmd_get_sub_id(st_cmd);
			helper.dev_id = id;
			helper.step = FU_LEGION_HID_UPGRADE_STEP_WRITE_DATA;
			if (!fu_legion_hid_device_read_upgrade_response(self, &helper, error))
				return FALSE;
			recieved_size = fu_memread_uint32(res->data + 10, G_BIG_ENDIAN);
			if (recieved_size != send_size) {
				g_set_error(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "device report received size %u mismatch send size %u",
					    recieved_size,
					    send_size);
				return FALSE;
			}
		}
	}

	return TRUE;
}

static gboolean
fu_legion_hid_device_upgrade_write_data(FuLegionHidDevice *self,
					guint8 id,
					guint max_size,
					GBytes *payload,
					GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	if (max_size % FU_LEGION_HID_DEVICE_FW_PACKET_LENGTH != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "device report max size %u is invalid",
			    max_size);
		return FALSE;
	}

	chunks = fu_chunk_array_new_from_bytes(payload,
					       FU_CHUNK_ADDR_OFFSET_NONE,
					       FU_CHUNK_PAGESZ_NONE,
					       FU_LEGION_HID_DEVICE_FW_PACKET_LENGTH);
	if (chunks == NULL)
		return FALSE;

	if (!fu_legion_hid_device_upgrade_write_data_chunks(self, id, max_size, chunks, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_legion_hid_device_upgrade_verify(FuLegionHidDevice *self, guint8 id, GError **error)
{
	g_autoptr(GByteArray) res = NULL;
	g_autoptr(FuStructLegionHidUpgradeCmd) st_cmd = fu_struct_legion_hid_upgrade_cmd_new();
	FuLegionHidUpgradeRetryHelper helper = {0};
	guint8 status = 0;
	guint8 content[] = {0x02, FU_LEGION_HID_UPGRADE_STEP_VERIFY, FU_LEGION_HID_CMD_CONSTANT_SN};

	fu_struct_legion_hid_upgrade_cmd_set_length(st_cmd, sizeof(content) + 5);
	fu_struct_legion_hid_upgrade_cmd_set_device_id(st_cmd, id);
	if (!fu_struct_legion_hid_upgrade_cmd_set_data(st_cmd, content, sizeof(content), error))
		return FALSE;
	if (!fu_udev_device_write(FU_UDEV_DEVICE(self),
				  st_cmd->buf->data,
				  st_cmd->buf->len,
				  FU_LEGION_HID_DEVICE_IO_TIMEOUT,
				  FU_IO_CHANNEL_FLAG_NONE,
				  error))
		return FALSE;

	res = g_byte_array_sized_new(FU_LEGION_HID_DEVICE_FW_REPORT_LENGTH);
	helper.res = res;
	helper.main_id = fu_struct_legion_hid_upgrade_cmd_get_main_id(st_cmd);
	helper.sub_id = fu_struct_legion_hid_upgrade_cmd_get_sub_id(st_cmd);
	helper.dev_id = id;
	helper.step = FU_LEGION_HID_UPGRADE_STEP_VERIFY;
	if (!fu_legion_hid_device_read_upgrade_response(self, &helper, error))
		return FALSE;
	status = res->data[9];
	if (status != FU_LEGION_HID_RESPONSE_STATUS_OK) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "device report verify command failed with %u",
			    status);
		return FALSE;
	}

	return TRUE;
}

static guint
fu_legion_hid_device_get_version(FuLegionHidDevice *self, guint8 id, GError **error)
{
	guint version = 0;
	g_autoptr(GByteArray) res = NULL;
	g_autoptr(FuStructLegionHidNormalCmd) st_cmd = fu_struct_legion_hid_normal_cmd_new();
	FuLegionHidNormalRetryHelper helper = {0};
	guint8 content[] = {FU_LEGION_HID_CMD_CONSTANT_SN};

	fu_struct_legion_hid_normal_cmd_set_length(st_cmd, sizeof(content) + 4);
	fu_struct_legion_hid_normal_cmd_set_main_id(st_cmd, 0x79);
	fu_struct_legion_hid_normal_cmd_set_sub_id(st_cmd, 0x01);
	fu_struct_legion_hid_normal_cmd_set_device_id(st_cmd, id);
	if (!fu_struct_legion_hid_normal_cmd_set_data(st_cmd, content, sizeof(content), error))
		return version;
	if (!fu_udev_device_write(FU_UDEV_DEVICE(self),
				  st_cmd->buf->data,
				  st_cmd->buf->len,
				  FU_LEGION_HID_DEVICE_IO_TIMEOUT,
				  FU_IO_CHANNEL_FLAG_NONE,
				  error))
		return version;

	res = g_byte_array_sized_new(FU_LEGION_HID_DEVICE_FW_REPORT_LENGTH);
	helper.res = res;
	helper.main_id = fu_struct_legion_hid_normal_cmd_get_main_id(st_cmd);
	helper.sub_id = fu_struct_legion_hid_normal_cmd_get_sub_id(st_cmd);
	helper.dev_id = id;
	if (!fu_legion_hid_device_read_response(self, &helper, error))
		return version;

	version = fu_memread_uint32(res->data + 13, G_BIG_ENDIAN);
	return version;
}

static gboolean
fu_legion_hid_device_ensure_version(FuLegionHidDevice *self, GError **error)
{
	guint mcu_version, left_version, right_version, final_version;
	char version[32] = {0};

	/* Waiting for gamepad to reconnect to MCU */
	if (fu_legion_hid_device_get_version(self, FU_LEGION_HID_DEVICE_ID_GAMEPAD_L, error) == 0 ||
	    fu_legion_hid_device_get_version(self, FU_LEGION_HID_DEVICE_ID_GAMEPAD_R, error) == 0) {
		/* Clear error for retrying later */
		g_clear_error(error);
		g_usleep(FU_LEGION_HID_DEVICE_REBOOT_WAIT_TIME * 1000);
	}

	mcu_version = fu_legion_hid_device_get_version(self, FU_LEGION_HID_DEVICE_ID_RX, error);
	if (mcu_version == 0)
		return FALSE;
	left_version =
	    fu_legion_hid_device_get_version(self, FU_LEGION_HID_DEVICE_ID_GAMEPAD_L, error);
	if (left_version == 0)
		return FALSE;
	right_version =
	    fu_legion_hid_device_get_version(self, FU_LEGION_HID_DEVICE_ID_GAMEPAD_R, error);
	if (right_version == 0)
		return FALSE;

	final_version = mcu_version + left_version + right_version;
	g_snprintf(version, sizeof(version), "%u", final_version);
	fu_device_set_version(FU_DEVICE(self), version);
	return TRUE;
}

static guint8
fu_legion_hid_device_get_id(GByteArray *buffer)
{
	guint offset = 0;
	guint8 id = 0;

	if (buffer->len < FU_LEGION_HID_DEVICE_FW_SIGNED_LENGTH + FU_LEGION_HID_DEVICE_FW_ID_LENGTH)
		return 0;

	offset =
	    buffer->len - FU_LEGION_HID_DEVICE_FW_SIGNED_LENGTH - FU_LEGION_HID_DEVICE_FW_ID_LENGTH;
	for (guint i = 0; i < FU_LEGION_HID_DEVICE_FW_ID_LENGTH; ++i) {
		id = buffer->data[offset + i];
		if (id == FU_LEGION_HID_DEVICE_ID_DONGLE ||
		    id == FU_LEGION_HID_DEVICE_ID_GAMEPAD_L3 ||
		    id == FU_LEGION_HID_DEVICE_ID_GAMEPAD_R3)
			return id;
	}

	return 0;
}

static gboolean
fu_legion_hid_device_execute_upgrade(FuLegionHidDevice *self, FuFirmware *firmware, GError **error)
{
	g_autoptr(GBytes) payload = NULL;
	g_autoptr(GByteArray) array = NULL;
	guint8 id = 0;
	guint16 crc16 = 0;
	guint max_size = 0;

	payload = fu_firmware_get_bytes(firmware, error);
	if (payload == NULL)
		return FALSE;

	array = g_byte_array_new();
	fu_byte_array_append_bytes(array, payload);

	id = fu_legion_hid_device_get_id(array);
	if (id == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "firmware device id is invalid");
		return FALSE;
	}

	crc16 = fu_crc16(FU_CRC_KIND_B16_XMODEM, array->data, array->len);

	if (!fu_legion_hid_device_upgrade_start(self, id, crc16, array->len, error))
		return FALSE;

	if (!fu_legion_hid_device_upgrade_query_size(self, id, &max_size, error))
		return FALSE;

	if (!fu_legion_hid_device_upgrade_write_data(self, id, max_size, payload, error))
		return FALSE;

	if (!fu_legion_hid_device_upgrade_verify(self, id, error))
		return FALSE;

	return TRUE;
}

static gboolean
fu_legion_hid_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuLegionHidDevice *self = FU_LEGION_HID_DEVICE(device);

	g_autoptr(FuFirmware) img_mcu = NULL;
	g_autoptr(FuFirmware) img_left = NULL;
	g_autoptr(FuFirmware) img_right = NULL;
	gboolean mcu_need_upgrade = FALSE;
	gboolean left_need_upgrade = FALSE;
	gboolean right_need_upgrade = FALSE;
	guint device_count = 0;

	img_mcu = fu_firmware_get_image_by_id(firmware, "DeviceIDRx", error);
	if (img_mcu != NULL) {
		guint raw_version = fu_firmware_get_version_raw(img_mcu);
		guint mcu_version = fu_legion_hid_device_get_version(self, 1, error);
		if (raw_version > mcu_version) {
			mcu_need_upgrade = TRUE;
			device_count++;
		}
	}

	img_left = fu_firmware_get_image_by_id(firmware, "DeviceIDGamepadL", error);
	if (img_left != NULL) {
		guint raw_version = fu_firmware_get_version_raw(img_left);
		guint left_version = fu_legion_hid_device_get_version(self, 3, error);
		if (raw_version > left_version) {
			left_need_upgrade = TRUE;
			device_count++;
		}
	}

	img_right = fu_firmware_get_image_by_id(firmware, "DeviceIDGamepadR", error);
	if (img_right != NULL) {
		guint raw_version = fu_firmware_get_version_raw(img_right);
		guint right_version = fu_legion_hid_device_get_version(self, 4, error);
		if (raw_version > right_version) {
			right_need_upgrade = TRUE;
			device_count++;
		}
	}

	if (device_count > 0) {
		fu_progress_set_id(progress, G_STRLOC);
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10, NULL);
		for (guint i = 0; i < device_count; ++i)
			fu_progress_add_step(progress,
					     FWUPD_STATUS_DEVICE_WRITE,
					     90 / device_count,
					     NULL);

		fu_progress_step_done(progress);
	}

	if (left_need_upgrade) {
		if (!fu_legion_hid_device_execute_upgrade(self, img_left, error)) {
			g_prefix_error_literal(error, "execute upgrade left gamepad failed: ");
			return FALSE;
		}
		fu_progress_step_done(progress);
		g_usleep(FU_LEGION_HID_DEVICE_REBOOT_WAIT_TIME * 1000);
	}

	if (right_need_upgrade) {
		if (!fu_legion_hid_device_execute_upgrade(self, img_right, error)) {
			g_prefix_error_literal(error, "execute upgrade right gamepad failed: ");
			return FALSE;
		}
		fu_progress_step_done(progress);
		g_usleep(FU_LEGION_HID_DEVICE_REBOOT_WAIT_TIME * 1000);
	}

	if (mcu_need_upgrade) {
		if (!fu_legion_hid_device_execute_upgrade(self, img_mcu, error)) {
			g_prefix_error_literal(error, "execute upgrade mcu failed: ");
			return FALSE;
		}
		fu_progress_step_done(progress);
	} else {
		/*
		 * If only the controller is updated, the MCU will not restart,
		 * so the version number needs to be reset.
		 */
		if (!fu_legion_hid_device_ensure_version(self, error))
			return FALSE;
	}

	return TRUE;
}

static void
fu_legion_hid_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gboolean
fu_legion_hid_device_validate_descriptor(FuDevice *self, GError **error)
{
	g_autoptr(FuHidDescriptor) descriptor = NULL;
	g_autoptr(FuHidReport) report = NULL;

	descriptor = fu_hidraw_device_parse_descriptor(FU_HIDRAW_DEVICE(self), error);
	if (descriptor == NULL)
		return FALSE;
	report = fu_hid_descriptor_find_report(descriptor,
					       error,
					       "usage-page",
					       0xFFA0,
					       "usage",
					       0x01,
					       "collection",
					       0x01,
					       NULL);
	if (report == NULL)
		return FALSE;

	return TRUE;
}

static gboolean
fu_legion_hid_device_setup(FuDevice *device, GError **error)
{
	FuLegionHidDevice *self = FU_LEGION_HID_DEVICE(device);

	if (!fu_legion_hid_device_validate_descriptor(device, error))
		return FALSE;

	if (!fu_legion_hid_device_ensure_version(self, error))
		return FALSE;

	return TRUE;
}

static void
fu_legion_hid_device_class_init(FuLegionHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_legion_hid_device_setup;
	device_class->write_firmware = fu_legion_hid_device_write_firmware;
	device_class->set_progress = fu_legion_hid_device_set_progress;
}

static void
fu_legion_hid_device_init(FuLegionHidDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.legion-hid");
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_LEGION_HID_FIRMWARE);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_NUMBER);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
}

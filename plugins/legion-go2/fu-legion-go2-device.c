/*
 * Copyright 2025 lazro <2059899519@qq.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#include "fu-legion-go2-device.h"
#include "fu-legion-go2-struct.h"
#include "fu-legion-go2-firmware.h"

struct _FuLegionGo2Device {
	FuHidrawDevice parent_instance;
};

G_DEFINE_TYPE(FuLegionGo2Device, fu_legion_go2_device, FU_TYPE_HIDRAW_DEVICE)

static gboolean
fu_legion_go2_device_read_normal_response_retry_func(FuDevice* device, gpointer user_data, GError** error)
{
	struct FuStructLegionGo2NormalRetryParam* param = (struct FuStructLegionGo2NormalRetryParam*)user_data;
	GByteArray* res = param->res;
	guint8 main_id = param->main_id;
	guint8 sub_id = param->sub_id;
	guint8 dev_id = param->dev_id;

	g_byte_array_set_size(res, FU_LEGION_GO2_DEVICE_FW_REPORT_LENGTH);
	if (!fu_udev_device_read(FU_UDEV_DEVICE(device),
				 res->data,
				 res->len,
				 NULL,
				 FU_LEGION_GO2_DEVICE_IO_TIMEOUT,
				 FU_IO_CHANNEL_FLAG_NONE,
				 error)) {
		g_message("fu_udev_device_read failed: %s",
			  (error && *error) ? (*error)->message : "unknown");
		return FALSE;
	}

	if (res->data[2] == main_id &&
	    res->data[3] == sub_id &&
	    res->data[4] == dev_id)
		return TRUE;

	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "response mismatch, retrying...");
	return FALSE;
}

static gboolean
fu_legion_go2_device_read_response(FuLegionGo2Device *self, struct FuStructLegionGo2NormalRetryParam* param, GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_legion_go2_device_read_normal_response_retry_func,
				    5,
				    0,
			            (gpointer)param,
				    error);
}

static gboolean
fu_legion_go2_device_read_upgrade_response_retry_func(FuDevice* device, gpointer user_data, GError** error)
{
	struct FuStructLegionGo2UpgradeRetryParam* param = (struct FuStructLegionGo2UpgradeRetryParam*)user_data;
	GByteArray* res = param->res;
	guint8 main_id = param->main_id;
	guint8 sub_id = param->sub_id;
	guint8 dev_id = param->dev_id;
	guint8 step = param->step;
	gboolean is_valid_dev_id = FALSE;

	g_byte_array_set_size(res, FU_LEGION_GO2_DEVICE_FW_REPORT_LENGTH);
	if (!fu_udev_device_read(FU_UDEV_DEVICE(device),
				 res->data,
				 res->len,
				 NULL,
				 FU_LEGION_GO2_DEVICE_IO_TIMEOUT,
				 FU_IO_CHANNEL_FLAG_NONE,
				 error)) {
		g_message("fu_udev_device_read failed: %s",
			  (error && *error) ? (*error)->message : "unknown");
		return FALSE;
	}

	is_valid_dev_id = (res->data[4] == dev_id) ||
	                  (res->data[4] == 3 && (dev_id == 5 || dev_id == 7)) ||
			  (res->data[4] == 4 && (dev_id == 6 || dev_id == 8));

	if (res->data[2] == main_id &&
	    res->data[3] == sub_id &&
	    is_valid_dev_id &&
	    res->data[7] == step &&
	    res->data[9] != FU_LEGION_GO2_RESPONSE_STATUS_BUSY)
		return TRUE;

	g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "response mismatch, retrying...");
	return FALSE;
}

static gboolean
fu_legion_go2_device_read_upgrade_response(FuLegionGo2Device *self, struct FuStructLegionGo2UpgradeRetryParam* param, GError **error)
{
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_legion_go2_device_read_upgrade_response_retry_func,
				    120,
				    0,
			            (gpointer)param,
				    error);
}

static gboolean
fu_legion_go2_device_upgrade_start(FuLegionGo2Device *self, guint8 id, guint16 crc16, guint size, GError **error)
{
	g_autoptr(GByteArray) res = NULL;
	g_autoptr(FuStructLegionGo2UpgradeCmd) cmd = fu_struct_legion_go2_upgrade_cmd_new();
	struct FuStructLegionGo2UpgradeRetryParam param = { 0 };
	guint8 status = 0;
	guint8 content[] = {0x08,
			    FU_LEGION_GO2_UPGRADE_STEP_START,
			    0x00,
			    crc16 >> 8 & 0xff,
			    crc16 & 0xff,
			    size >> 16 & 0xff,
			    size >> 8 & 0xff,
			    size & 0xff,
			    0x01};
	fu_struct_legion_go2_upgrade_cmd_set_report_id(cmd, 5);
	fu_struct_legion_go2_upgrade_cmd_set_length(cmd, sizeof(content) + 5);
	fu_struct_legion_go2_upgrade_cmd_set_device_id(cmd, id);
	fu_struct_legion_go2_upgrade_cmd_set_param(cmd, 0x01);
	fu_struct_legion_go2_upgrade_cmd_set_data(cmd, content, sizeof(content), error);
	if (!fu_udev_device_write(FU_UDEV_DEVICE(self),
				  cmd->buf->data,
			          cmd->buf->len,
				  FU_LEGION_GO2_DEVICE_IO_TIMEOUT,
				  FU_IO_CHANNEL_FLAG_NONE,
				  error)) {
		g_message("fu_udev_device_write failed: %s",
			  (error && *error) ? (*error)->message : "unknown");
		return FALSE;
	}

	res = g_byte_array_sized_new(FU_LEGION_GO2_DEVICE_FW_REPORT_LENGTH);
	param.res = res;
	param.main_id = fu_struct_legion_go2_upgrade_cmd_get_main_id(cmd);
	param.sub_id = fu_struct_legion_go2_upgrade_cmd_get_sub_id(cmd);
	param.dev_id = id;
	param.step = FU_LEGION_GO2_UPGRADE_STEP_START;
	if (!fu_legion_go2_device_read_upgrade_response(self, &param, error))
	{
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "read start command response failed");
		return FALSE;
	}
	status = param.res->data[9];
	if (status != FU_LEGION_GO2_RESPONSE_STATUS_OK)
	{
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "device report start command failed");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_legion_go2_device_upgrade_query_size(FuLegionGo2Device *self, guint8 id, guint* max_size, GError **error)
{
	g_autoptr(GByteArray) res = NULL;
	g_autoptr(FuStructLegionGo2UpgradeCmd) cmd = fu_struct_legion_go2_upgrade_cmd_new();
	struct FuStructLegionGo2UpgradeRetryParam param = { 0 };
	guint8 status = 0;
	guint8 content[] = {0x02, FU_LEGION_GO2_UPGRADE_STEP_QUERY_SIZE, 0x01};
	fu_struct_legion_go2_upgrade_cmd_set_report_id(cmd, 5);
	fu_struct_legion_go2_upgrade_cmd_set_length(cmd, sizeof(content) + 5);
	fu_struct_legion_go2_upgrade_cmd_set_device_id(cmd, id);
	fu_struct_legion_go2_upgrade_cmd_set_param(cmd, 0x01);
	fu_struct_legion_go2_upgrade_cmd_set_data(cmd, content, sizeof(content), error);
	if (!fu_udev_device_write(FU_UDEV_DEVICE(self),
				  cmd->buf->data,
			          cmd->buf->len,
				  FU_LEGION_GO2_DEVICE_IO_TIMEOUT,
				  FU_IO_CHANNEL_FLAG_NONE,
				  error)) {
		g_message("fu_udev_device_write failed: %s",
			  (error && *error) ? (*error)->message : "unknown");
		return FALSE;
	}

	res = g_byte_array_sized_new(FU_LEGION_GO2_DEVICE_FW_REPORT_LENGTH);
	param.res = res;
	param.main_id = fu_struct_legion_go2_upgrade_cmd_get_main_id(cmd);
	param.sub_id = fu_struct_legion_go2_upgrade_cmd_get_sub_id(cmd);
	param.dev_id = id;
	param.step = FU_LEGION_GO2_UPGRADE_STEP_QUERY_SIZE;
	if (!fu_legion_go2_device_read_upgrade_response(self, &param, error))
	{
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "read query size command response failed");
		return FALSE;
	}
	status = param.res->data[9];
	if (status == FU_LEGION_GO2_RESPONSE_STATUS_FAIL)
	{
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "device report query size command failed");
		return FALSE;
	}
	if (max_size)
		*max_size = param.res->data[9] << 8 | param.res->data[10];

	return TRUE;
}

static gboolean
fu_legion_go2_device_upgrade_write_data(FuLegionGo2Device *self, guint8 id, GByteArray *buffer, guint max_size, GError **error)
{
	guint size = buffer->len;
	guint inner_loop_times = max_size / FU_LEGION_GO2_DEVICE_FW_PACKET_LENGTH;
	guint outer_loop_times = size / max_size;
	guint send_size = 0;
	guint ready_send_size = 0;
	guint recieved_size = 0;
	g_autoptr(FuStructLegionGo2UpgradeCmd) cmd = fu_struct_legion_go2_upgrade_cmd_new();
	g_autoptr(GByteArray) res = g_byte_array_sized_new(FU_LEGION_GO2_DEVICE_FW_REPORT_LENGTH);
	guint8 content[FU_LEGION_GO2_DEVICE_FW_PACKET_LENGTH + 1] = { 0 };
	struct FuStructLegionGo2UpgradeRetryParam param = { 0 };
	gboolean wait = FALSE;

	if (max_size % FU_LEGION_GO2_DEVICE_FW_PACKET_LENGTH != 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "device report max size invalid");
		return FALSE;
	}
	g_message("device report max size: %u", max_size);
	
	if (size % max_size != 0)
		outer_loop_times += 1;
	for (guint i = 0; i < outer_loop_times; ++i)
	{
		for (guint j = 0; j < inner_loop_times; ++j)
		{
			if (send_size >= size)
			{
				break;
			}
			memcpy(content, buffer->data + send_size, FU_LEGION_GO2_DEVICE_FW_PACKET_LENGTH);
			content[FU_LEGION_GO2_DEVICE_FW_PACKET_LENGTH] = 0x01;
			fu_struct_legion_go2_upgrade_cmd_set_report_id(cmd, 5);
			fu_struct_legion_go2_upgrade_cmd_set_length(cmd, sizeof(content) + 5);
			fu_struct_legion_go2_upgrade_cmd_set_device_id(cmd, id);
			fu_struct_legion_go2_upgrade_cmd_set_param(cmd, 0x02);
			fu_struct_legion_go2_upgrade_cmd_set_data(cmd, content, sizeof(content), error);
			ready_send_size = send_size + FU_LEGION_GO2_DEVICE_FW_PACKET_LENGTH;
			wait = (ready_send_size % max_size == 0) || (ready_send_size >= size);
			if (!fu_udev_device_write(FU_UDEV_DEVICE(self),
						  cmd->buf->data,
						  cmd->buf->len,
						  FU_LEGION_GO2_DEVICE_IO_TIMEOUT,
						  FU_IO_CHANNEL_FLAG_NONE,
						  error)) {
				g_message("fu_udev_device_write failed: %s",
					  (error && *error) ? (*error)->message : "unknown");
				return FALSE;
			}
			send_size += FU_LEGION_GO2_DEVICE_FW_PACKET_LENGTH;
			if (wait)
			{
				param.res = res;
				param.main_id = fu_struct_legion_go2_upgrade_cmd_get_main_id(cmd);
				param.sub_id = fu_struct_legion_go2_upgrade_cmd_get_sub_id(cmd);
				param.dev_id = id;
				param.step = FU_LEGION_GO2_UPGRADE_STEP_WRITE_DATA;
				if (!fu_legion_go2_device_read_upgrade_response(self, &param, error))
				{
					g_set_error_literal(error,
							    FWUPD_ERROR,
							    FWUPD_ERROR_READ,
							    "read write data command response failed");
					return FALSE;
				}
				recieved_size = res->data[10] << 24 | res->data[11] << 16 |
						res->data[12] << 8 | res->data[13];
				if (recieved_size != send_size && recieved_size != size)
				{
					g_set_error_literal(error,
							    FWUPD_ERROR,
							    FWUPD_ERROR_INTERNAL,
							    "device report recieved size mismatch send size");
					return FALSE;
				}
			}
		}
	}

	return TRUE;
}

static gboolean
fu_legion_go2_device_upgrade_verify(FuLegionGo2Device *self, guint8 id, GError **error)
{
	g_autoptr(GByteArray) res = NULL;
	g_autoptr(FuStructLegionGo2UpgradeCmd) cmd = fu_struct_legion_go2_upgrade_cmd_new();
	struct FuStructLegionGo2UpgradeRetryParam param = { 0 };
	guint8 status = 0;
	guint8 content[] = {0x02, FU_LEGION_GO2_UPGRADE_STEP_VERIFY, 0x01};
	fu_struct_legion_go2_upgrade_cmd_set_report_id(cmd, 5);
	fu_struct_legion_go2_upgrade_cmd_set_length(cmd, sizeof(content) + 5);
	fu_struct_legion_go2_upgrade_cmd_set_device_id(cmd, id);
	fu_struct_legion_go2_upgrade_cmd_set_param(cmd, 0x01);
	fu_struct_legion_go2_upgrade_cmd_set_data(cmd, content, sizeof(content), error);
	if (!fu_udev_device_write(FU_UDEV_DEVICE(self),
				  cmd->buf->data,
			          cmd->buf->len,
				  FU_LEGION_GO2_DEVICE_IO_TIMEOUT,
				  FU_IO_CHANNEL_FLAG_NONE,
				  error)) {
		g_message("fu_udev_device_write failed: %s",
			  (error && *error) ? (*error)->message : "unknown");
		return FALSE;
	}

	res = g_byte_array_sized_new(FU_LEGION_GO2_DEVICE_FW_REPORT_LENGTH);
	param.res = res;
	param.main_id = fu_struct_legion_go2_upgrade_cmd_get_main_id(cmd);
	param.sub_id = fu_struct_legion_go2_upgrade_cmd_get_sub_id(cmd);
	param.dev_id = id;
	param.step = FU_LEGION_GO2_UPGRADE_STEP_VERIFY;
	if (!fu_legion_go2_device_read_upgrade_response(self, &param, error))
	{
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "read verify command response failed");
		return FALSE;
	}
	status = param.res->data[9];
	if (status != FU_LEGION_GO2_RESPONSE_STATUS_OK)
	{
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "device report verify command failed");
		return FALSE;
	}

	return TRUE;
}

static guint
fu_legion_go2_device_get_version(FuLegionGo2Device *self, guint8 id, GError **error)
{
	guint version = 0;
	g_autoptr(GByteArray) res = NULL;
	g_autoptr(FuStructLegionGo2NormalCmd) cmd = fu_struct_legion_go2_normal_cmd_new();
	struct FuStructLegionGo2NormalRetryParam param = { 0 };
	guint8 content[] = {0x01};
	fu_struct_legion_go2_normal_cmd_set_report_id(cmd, 5);
	fu_struct_legion_go2_normal_cmd_set_length(cmd, sizeof(content) + 4);
	fu_struct_legion_go2_normal_cmd_set_main_id(cmd, 0x79);
	fu_struct_legion_go2_normal_cmd_set_sub_id(cmd, 0x01);
	fu_struct_legion_go2_normal_cmd_set_device_id(cmd, id);
	fu_struct_legion_go2_normal_cmd_set_data(cmd, content, sizeof(content), error);
	if (!fu_udev_device_write(FU_UDEV_DEVICE(self),
				  cmd->buf->data,
				  cmd->buf->len,
				  FU_LEGION_GO2_DEVICE_IO_TIMEOUT,
				  FU_IO_CHANNEL_FLAG_NONE,
				  error)) {
		g_message("fu_udev_device_write failed: %s",
			  (error && *error) ? (*error)->message : "unknown");
		return version;
	}

	res = g_byte_array_sized_new(FU_LEGION_GO2_DEVICE_FW_REPORT_LENGTH);
	param.res = res;
	param.main_id = fu_struct_legion_go2_normal_cmd_get_main_id(cmd);
	param.sub_id = fu_struct_legion_go2_normal_cmd_get_sub_id(cmd);
	param.dev_id = id;
	if (!fu_legion_go2_device_read_response(self, &param, error))
	{
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_READ,
				    "read version command response failed");
		return version;
	}

	version = res->data[13] << 24 | res->data[14] << 16 |
		  res->data[15] << 8 | res->data[16];
	g_message("device %u version: %u", id, version);

	return version;
}

static void
fu_legion_go2_device_set_version(FuLegionGo2Device *device, GError **error)
{
	guint mcu_version, left_version, right_version, version;
	gchar szVersion[32] = { 0 };

	mcu_version = fu_legion_go2_device_get_version(device, 1, error);
	left_version = fu_legion_go2_device_get_version(device, 3, error);
	right_version = fu_legion_go2_device_get_version(device, 4, error);

	version = mcu_version + left_version + right_version;
	g_snprintf(szVersion,
		   sizeof(szVersion),
		   "%x.%02x.%02x.%02x",
		   version >> 24 & 0xff,
		   version >> 16 & 0xff,
		   version >> 8 & 0xff,
		   version & 0xff);
	fu_device_set_version(FU_DEVICE(device), szVersion);
}

static guint8 fu_legion_go2_device_get_id(GByteArray* buffer)
{
	guint offset = buffer->len - FU_LEGION_GO2_DEVICE_FW_SIGNED_LENGTH - FU_LEGION_GO2_DEVICE_FW_ID_LENGTH;
	guint8 id = 0;

	if (buffer->len < FU_LEGION_GO2_DEVICE_FW_SIGNED_LENGTH + FU_LEGION_GO2_DEVICE_FW_ID_LENGTH)
	{
		return 0;
	}

	for (guint i = 0; i < FU_LEGION_GO2_DEVICE_FW_ID_LENGTH; ++i)
	{
		id = buffer->data[offset + i];
		if (id == 2 || id == 7 || id == 8)
		{
			return id;
		}
	}

	return 0;
}

static gboolean
fu_legion_go2_device_execute_upgrade(FuLegionGo2Device* self, FuFirmware *firmware, GError **error)
{
	g_autoptr(GBytes) payload = fu_firmware_get_bytes(firmware, error);
	gsize size = 0;
	const guint8 *data = NULL;
	g_autoptr(GByteArray) array = NULL;
	guint8 id = 0;
	guint16 crc16 = 0;
	guint max_size = 0;
	if (payload == NULL || g_bytes_get_size(payload) == 0)
	{
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "firmware data is invalid");
		return FALSE;
	}

	data = g_bytes_get_data(payload, &size);
	array = g_byte_array_sized_new(size);
	g_byte_array_append(array, data, size);

	id = fu_legion_go2_device_get_id(array);
	if (id == 0)
	{
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "firmware device id is invalid");
		return FALSE;
	}
	g_message("firmware device id: %u", id);

	crc16 = fu_crc16(FU_CRC_KIND_B16_XMODEM, array->data, array->len);
	g_message("firmware crc16: %u and firmware size: %lu", crc16, size);

	if (!fu_legion_go2_device_upgrade_start(self, id, crc16, size, error))
		return FALSE;
	g_message("start step done");

	max_size = 0;
	if (!fu_legion_go2_device_upgrade_query_size(self, id, &max_size, error))
		return FALSE;
	g_message("query size step done");

	if (!fu_legion_go2_device_upgrade_write_data(self, id, array, max_size, error))
		return FALSE;
	g_message("write data step done");

	if (!fu_legion_go2_device_upgrade_verify(self, id, error))
		return FALSE;
	g_message("verify step done");

	return TRUE;
}

static gboolean
fu_legion_go2_device_write_firmware(FuDevice *device,
                                    FuFirmware *firmware,
                                    FuProgress *progress,
                                    FwupdInstallFlags flags,
                                    GError **error)
{
	FuLegionGo2Device* self = FU_LEGION_GO2_DEVICE(device);

	FuFirmware* img_mcu = NULL;
	FuFirmware* img_left = NULL;
	FuFirmware* img_right = NULL;
	gboolean mcu_need_upgrade = FALSE;
	gboolean left_need_upgrade = FALSE;
	gboolean right_need_upgrade = FALSE;
	guint device_count = 0;

	img_mcu = fu_firmware_get_image_by_id(firmware, "DeviceIDRx", error);
	if (img_mcu)
	{
		guint raw_version = fu_firmware_get_version_raw(img_mcu);
		guint mcu_version = fu_legion_go2_device_get_version(self, 1, error);
		if (raw_version > mcu_version)
		{
			mcu_need_upgrade = TRUE;
			device_count++;
		}
	}

	img_left = fu_firmware_get_image_by_id(firmware, "DeviceIDGamepadL", error);
	if (img_left)
	{
		guint raw_version = fu_firmware_get_version_raw(img_left);
		guint left_version = fu_legion_go2_device_get_version(self, 3, error);
		if (raw_version > left_version)
		{
			left_need_upgrade = TRUE;
			device_count++;
		}
	}

	img_right = fu_firmware_get_image_by_id(firmware, "DeviceIDGamepadR", error);
	if (img_right)
	{
		guint raw_version = fu_firmware_get_version_raw(img_right);
		guint right_version = fu_legion_go2_device_get_version(self, 4, error);
		if (raw_version > right_version)
		{
			right_need_upgrade = TRUE;
			device_count++;
		}
	}

	if (device_count == 0)
	{
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "no device need upgrade");
		return FALSE;
	}

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10, NULL);
	for (guint i = 0; i < device_count; ++i)
	{
		fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90 / device_count, NULL);
	}

	fu_progress_step_done(progress);

	if (left_need_upgrade)
	{
		if (!fu_legion_go2_device_execute_upgrade(self, img_left, error))
		{
			g_set_error_literal(error,
				            FWUPD_ERROR,
				            FWUPD_ERROR_INTERNAL,
				            "execute upgrade left gamepad failed");
			return FALSE;
		}
		fu_progress_step_done(progress);
		g_usleep(FU_LEGION_GO2_DEVICE_REBOOT_WAIT_TIME * 1000);
	}

	if (right_need_upgrade)
	{
		if (!fu_legion_go2_device_execute_upgrade(self, img_right, error))
		{
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "execute upgrade right gamepad failed");
			return FALSE;
		}
		fu_progress_step_done(progress);
		g_usleep(FU_LEGION_GO2_DEVICE_REBOOT_WAIT_TIME * 1000);
	}

	if (mcu_need_upgrade)
	{
		if (!fu_legion_go2_device_execute_upgrade(self, img_mcu, error))
		{
			g_set_error_literal(error,
					    FWUPD_ERROR,
					    FWUPD_ERROR_INTERNAL,
					    "execute upgrade mcu failed");
			return FALSE;
		}
		fu_progress_step_done(progress);
	}
	else
	{
		fu_legion_go2_device_set_version(self, error);
	}

	return TRUE;
}

static void
fu_legion_go2_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gboolean
fu_legion_go2_device_validate_descriptor(FuDevice *device, GError **error)
{
	g_autoptr(FuHidDescriptor) descriptor = NULL;
	g_autoptr(FuHidReport) report = NULL;

	descriptor = fu_hidraw_device_parse_descriptor(FU_HIDRAW_DEVICE(device), error);
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
fu_legion_go2_device_setup(FuDevice *device, GError **error)
{
	if (!fu_legion_go2_device_validate_descriptor(device, error))
		return FALSE;

	fu_legion_go2_device_set_version(FU_LEGION_GO2_DEVICE(device), error);

	return TRUE;
}

static FuFirmware *
fu_legion_go2_device_prepare_firmware(FuDevice *device,
				      GInputStream *stream,
				      FuProgress *progress,
				      FuFirmwareParseFlags flags,
				      GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_legion_go2_firmware_new();

	/* sanity check */
	if (!fu_firmware_parse_stream(firmware, stream, 0x0, flags, error))
		return NULL;

	return g_steal_pointer(&firmware);
}

static void
fu_legion_go2_device_class_init(FuLegionGo2DeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_legion_go2_device_setup;
	device_class->write_firmware = fu_legion_go2_device_write_firmware;
	device_class->set_progress = fu_legion_go2_device_set_progress;
	device_class->prepare_firmware = fu_legion_go2_device_prepare_firmware;
}

static void
fu_legion_go2_device_init(FuLegionGo2Device *self)
{
	fu_device_set_name(FU_DEVICE(self), "Legion Go2 MCU");
	fu_device_set_vendor(FU_DEVICE(self), "Lenovo");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.legion-go2");

	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
}

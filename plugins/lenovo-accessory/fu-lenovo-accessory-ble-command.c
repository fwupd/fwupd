/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-accessory-ble-command.h"
#include "fu-lenovo-accessory-struct.h"

#define UUID_WRITE "c1d02501-2d1f-400a-95d2-6a2f7bca0c25"
#define UUID_READ  "c1d02502-2d1f-400a-95d2-6a2f7bca0c25"

static gboolean
fu_lenovo_accessory_ble_command_poll_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuBluezDevice *ble_device = FU_BLUEZ_DEVICE(device);
	GByteArray *buffer = (GByteArray *)user_data;
	g_autoptr(GByteArray) res = NULL;
	guint8 status;
	res = fu_bluez_device_read(ble_device, UUID_READ, error);
	if (res == NULL)
		return FALSE;
	if (res->len < 1) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_READ, "received empty data");
		return FALSE;
	}
	status = res->data[0] & 0x0F;
	if (status == FU_LENOVO_STATUS_COMMAND_SUCCESSFUL) {
		g_byte_array_set_size(buffer, 0);
		g_byte_array_append(buffer, res->data, res->len);
		return TRUE;
	}
	if (status == FU_LENOVO_STATUS_COMMAND_BUSY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "command busy");
		return FALSE;
	}
	g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_WRITE, "command failed: 0x%02x", status);
	return FALSE;
}

static gboolean
fu_lenovo_accessory_ble_command_process(FuBluezDevice *ble_device,
					GByteArray *buffer,
					FuIoctlFlags flags,
					GError **error)
{
	if (!fu_bluez_device_write(ble_device, UUID_WRITE, buffer, error)) {
		g_prefix_error_literal(error, "failed to write cmd: ");
		return FALSE;
	}
	return fu_device_retry_full(FU_DEVICE(ble_device),
				    fu_lenovo_accessory_ble_command_poll_cb,
				    50,	    /* count */
				    10,	    /* delay in ms */
				    buffer, /* user_data */
				    error);
}

gboolean
fu_lenovo_accessory_ble_command_fwversion(FuBluezDevice *ble_device,
					  guint8 *major,
					  guint8 *minor,
					  guint8 *internal,
					  GError **error)
{
	g_autoptr(FuLenovoAccessoryCmd) lenovo_hid_cmd = fu_lenovo_accessory_cmd_new();
	g_autoptr(FuLenovoBleFwVersion) lenovo_ble_fwversion = fu_lenovo_ble_fw_version_new();
	fu_lenovo_accessory_cmd_set_target_status(lenovo_hid_cmd, 0x00);
	fu_lenovo_accessory_cmd_set_data_size(lenovo_hid_cmd, 0x03);
	fu_lenovo_accessory_cmd_set_command_class(
	    lenovo_hid_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DEVICE_INFORMATION);
	fu_lenovo_accessory_cmd_set_command_id(lenovo_hid_cmd,
					       FU_LENOVO_ACCESSORY_INFO_ID_FIRMWARE_VERSION |
						   (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_GET << 7));
	fu_lenovo_accessory_cmd_set_flag_profile(lenovo_hid_cmd, 0x00);

	if (!fu_lenovo_ble_fw_version_set_cmd(lenovo_ble_fwversion, lenovo_hid_cmd, error))
		return FALSE;
	if (!fu_lenovo_accessory_ble_command_process(ble_device,
						     lenovo_ble_fwversion->buf,
						     FU_IOCTL_FLAG_RETRY,
						     error))
		return FALSE;
	if (major != NULL)
		*major = fu_lenovo_ble_fw_version_get_major(lenovo_ble_fwversion);
	if (minor != NULL)
		*minor = fu_lenovo_ble_fw_version_get_minor(lenovo_ble_fwversion);
	if (internal != NULL)
		*internal = fu_lenovo_ble_fw_version_get_internal(lenovo_ble_fwversion);
	return TRUE;
}

gboolean
fu_lenovo_accessory_ble_command_get_devicemode(FuBluezDevice *ble_device,
					       guint8 *mode,
					       GError **error)
{
	g_autoptr(FuLenovoAccessoryCmd) lenovo_hid_cmd = fu_lenovo_accessory_cmd_new();
	g_autoptr(FuLenovoBleDevicemode) lenovo_ble_mode = fu_lenovo_ble_devicemode_new();

	fu_lenovo_accessory_cmd_set_target_status(lenovo_hid_cmd, 0x00);
	fu_lenovo_accessory_cmd_set_data_size(lenovo_hid_cmd, 0x01);
	fu_lenovo_accessory_cmd_set_command_class(
	    lenovo_hid_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DEVICE_INFORMATION);
	fu_lenovo_accessory_cmd_set_command_id(lenovo_hid_cmd,
					       FU_LENOVO_ACCESSORY_INFO_ID_DEVICE_MODE |
						   (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));

	if (!fu_lenovo_ble_devicemode_set_cmd(lenovo_ble_mode, lenovo_hid_cmd, error))
		return FALSE;
	if (!fu_lenovo_accessory_ble_command_process(ble_device,
						     lenovo_ble_mode->buf,
						     FU_IOCTL_FLAG_RETRY,
						     error)) {
		return FALSE;
	}
	*mode = fu_lenovo_ble_devicemode_get_mode(lenovo_ble_mode);
	return TRUE;
}

gboolean
fu_lenovo_accessory_ble_command_dfu_set_devicemode(FuBluezDevice *ble_device,
						   guint8 mode,
						   GError **error)
{
	g_autoptr(FuLenovoAccessoryCmd) lenovo_hid_cmd = fu_lenovo_accessory_cmd_new();
	g_autoptr(FuLenovoBleData) lenovo_ble_data = fu_lenovo_ble_data_new();

	fu_lenovo_accessory_cmd_set_target_status(lenovo_hid_cmd, 0x00);
	fu_lenovo_accessory_cmd_set_data_size(lenovo_hid_cmd, 0x01);
	fu_lenovo_accessory_cmd_set_command_class(
	    lenovo_hid_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DEVICE_INFORMATION);
	fu_lenovo_accessory_cmd_set_command_id(lenovo_hid_cmd,
					       FU_LENOVO_ACCESSORY_INFO_ID_DEVICE_MODE |
						   (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));

	if (!fu_lenovo_ble_data_set_cmd(lenovo_ble_data, lenovo_hid_cmd, error))
		return FALSE;
	if (!fu_lenovo_ble_data_set_data(lenovo_ble_data, &mode, 1, error))
		return FALSE;
	if (mode == 0x02)
		return fu_bluez_device_write(ble_device, UUID_WRITE, lenovo_ble_data->buf, error);
	return fu_lenovo_accessory_ble_command_process(ble_device,
						       lenovo_ble_data->buf,
						       FU_IOCTL_FLAG_RETRY,
						       error);
}

gboolean
fu_lenovo_accessory_ble_command_dfu_exit(FuBluezDevice *ble_device,
					 guint8 exit_code,
					 GError **error)
{
	g_autoptr(FuLenovoAccessoryCmd) lenovo_hid_cmd = fu_lenovo_accessory_cmd_new();
	g_autoptr(FuLenovoBleData) lenovo_ble_data = fu_lenovo_ble_data_new();
	fu_lenovo_accessory_cmd_set_target_status(lenovo_hid_cmd, 0x00);
	fu_lenovo_accessory_cmd_set_data_size(lenovo_hid_cmd, 0x01);
	fu_lenovo_accessory_cmd_set_command_class(lenovo_hid_cmd,
						  FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_lenovo_accessory_cmd_set_command_id(lenovo_hid_cmd,
					       FU_LENOVO_ACCESSORY_DFU_ID_DFU_EXIT |
						   (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));

	if (!fu_lenovo_ble_data_set_cmd(lenovo_ble_data, lenovo_hid_cmd, error))
		return FALSE;
	if (!fu_lenovo_ble_data_set_data(lenovo_ble_data, &exit_code, 1, error))
		return FALSE;
	if (!fu_bluez_device_write(ble_device, UUID_WRITE, lenovo_ble_data->buf, error)) {
		g_prefix_error_literal(error, "failed to write cmd: ");
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_lenovo_accessory_ble_command_dfu_attribute(FuBluezDevice *ble_device,
					      guint8 *major_ver,
					      guint8 *minor_ver,
					      guint16 *product_pid,
					      guint8 *processor_id,
					      guint32 *app_max_size,
					      guint32 *page_size,
					      GError **error)
{
	g_autoptr(FuLenovoAccessoryCmd) lenovo_hid_cmd = fu_lenovo_accessory_cmd_new();
	g_autoptr(FuLenovoBleDfuAttribute) lenovo_ble_attribute = fu_lenovo_ble_dfu_attribute_new();

	fu_lenovo_accessory_cmd_set_data_size(lenovo_hid_cmd, 0x0D);
	fu_lenovo_accessory_cmd_set_command_class(lenovo_hid_cmd,
						  FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_lenovo_accessory_cmd_set_command_id(lenovo_hid_cmd,
					       FU_LENOVO_ACCESSORY_DFU_ID_DFU_ATTRIBUTE |
						   (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_GET << 7));

	if (!fu_lenovo_ble_dfu_attribute_set_cmd(lenovo_ble_attribute, lenovo_hid_cmd, error))
		return FALSE;

	if (!fu_lenovo_accessory_ble_command_process(ble_device,
						     lenovo_ble_attribute->buf,
						     FU_IOCTL_FLAG_RETRY,
						     error))
		return FALSE;

	if (major_ver != NULL)
		*major_ver = fu_lenovo_ble_dfu_attribute_get_major_ver(lenovo_ble_attribute);
	if (minor_ver != NULL)
		*minor_ver = fu_lenovo_ble_dfu_attribute_get_minor_ver(lenovo_ble_attribute);
	if (product_pid != NULL)
		*product_pid = fu_lenovo_ble_dfu_attribute_get_product_pid(lenovo_ble_attribute);
	if (processor_id != NULL)
		*processor_id = fu_lenovo_ble_dfu_attribute_get_processor_id(lenovo_ble_attribute);
	if (app_max_size != NULL)
		*app_max_size = fu_lenovo_ble_dfu_attribute_get_app_max_size(lenovo_ble_attribute);
	if (page_size != NULL)
		*page_size = fu_lenovo_ble_dfu_attribute_get_page_size(lenovo_ble_attribute);

	return TRUE;
}

gboolean
fu_lenovo_accessory_ble_command_dfu_prepare(FuBluezDevice *ble_device,
					    guint8 file_type,
					    guint32 start_address,
					    guint32 end_address,
					    guint32 crc32,
					    GError **error)
{
	g_autoptr(FuLenovoAccessoryCmd) lenovo_hid_cmd = fu_lenovo_accessory_cmd_new();
	g_autoptr(FuLenovoBleDfuPrepare) lenovo_ble_prepare = fu_lenovo_ble_dfu_prepare_new();

	fu_lenovo_accessory_cmd_set_data_size(lenovo_hid_cmd, 0x0D);
	fu_lenovo_accessory_cmd_set_command_class(lenovo_hid_cmd,
						  FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_lenovo_accessory_cmd_set_command_id(lenovo_hid_cmd,
					       FU_LENOVO_ACCESSORY_DFU_ID_DFU_PREPARE |
						   (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));

	if (!fu_lenovo_ble_dfu_prepare_set_cmd(lenovo_ble_prepare, lenovo_hid_cmd, error))
		return FALSE;
	fu_lenovo_ble_dfu_prepare_set_file_type(lenovo_ble_prepare, file_type);
	fu_lenovo_ble_dfu_prepare_set_start_address(lenovo_ble_prepare, start_address);
	fu_lenovo_ble_dfu_prepare_set_end_address(lenovo_ble_prepare, end_address);
	fu_lenovo_ble_dfu_prepare_set_crc32(lenovo_ble_prepare, crc32);
	return fu_lenovo_accessory_ble_command_process(ble_device,
						       lenovo_ble_prepare->buf,
						       FU_IOCTL_FLAG_RETRY,
						       error);
}

gboolean
fu_lenovo_accessory_ble_command_dfu_file(FuBluezDevice *ble_device,
					 guint8 file_type,
					 guint32 address,
					 const guint8 *file_data,
					 guint8 block_size,
					 GError **error)
{
	g_autoptr(FuLenovoAccessoryCmd) lenovo_hid_cmd = fu_lenovo_accessory_cmd_new();
	g_autoptr(FuLenovoBleDfuFw) lenovo_ble_file = fu_lenovo_ble_dfu_fw_new();

	fu_lenovo_accessory_cmd_set_target_status(lenovo_hid_cmd, 0x00);
	fu_lenovo_accessory_cmd_set_data_size(lenovo_hid_cmd, block_size + 5);
	fu_lenovo_accessory_cmd_set_command_class(lenovo_hid_cmd,
						  FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_lenovo_accessory_cmd_set_command_id(lenovo_hid_cmd,
					       FU_LENOVO_ACCESSORY_DFU_ID_DFU_FILE |
						   (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));

	if (!fu_lenovo_ble_dfu_fw_set_cmd(lenovo_ble_file, lenovo_hid_cmd, error))
		return FALSE;
	fu_lenovo_ble_dfu_fw_set_file_type(lenovo_ble_file, file_type);
	fu_lenovo_ble_dfu_fw_set_offset_address(lenovo_ble_file, address);
	if (!fu_lenovo_ble_dfu_fw_set_data(lenovo_ble_file, file_data, block_size, error))
		return FALSE;
	return fu_lenovo_accessory_ble_command_process(ble_device,
						       lenovo_ble_file->buf,
						       FU_IOCTL_FLAG_RETRY,
						       error);
}

gboolean
fu_lenovo_accessory_ble_command_dfu_crc(FuBluezDevice *ble_device, guint32 *crc32, GError **error)
{
	g_autoptr(FuLenovoAccessoryCmd) lenovo_hid_cmd = fu_lenovo_accessory_cmd_new();
	g_autoptr(FuLenovoBleDfuCrc) lenovo_ble_crc = fu_lenovo_ble_dfu_crc_new();

	fu_lenovo_accessory_cmd_set_data_size(lenovo_hid_cmd, 0x05);
	fu_lenovo_accessory_cmd_set_command_class(lenovo_hid_cmd,
						  FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_lenovo_accessory_cmd_set_command_id(lenovo_hid_cmd,
					       FU_LENOVO_ACCESSORY_DFU_ID_DFU_CRC |
						   (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_GET << 7));

	if (!fu_lenovo_ble_dfu_crc_set_cmd(lenovo_ble_crc, lenovo_hid_cmd, error))
		return FALSE;
	if (!fu_lenovo_accessory_ble_command_process(ble_device,
						     lenovo_ble_crc->buf,
						     FU_IOCTL_FLAG_RETRY,
						     error))
		return FALSE;
	if (crc32 != NULL)
		*crc32 = fu_lenovo_ble_dfu_crc_get_crc32(lenovo_ble_crc);
	return TRUE;
}

gboolean
fu_lenovo_accessory_ble_command_dfu_entry(FuBluezDevice *ble_device, GError **error)
{
	g_autoptr(FuLenovoAccessoryCmd) lenovo_hid_cmd = fu_lenovo_accessory_cmd_new();
	g_autoptr(FuLenovoBleData) lenovo_ble_data = fu_lenovo_ble_data_new();

	fu_lenovo_accessory_cmd_set_data_size(lenovo_hid_cmd, 0);
	fu_lenovo_accessory_cmd_set_command_class(lenovo_hid_cmd,
						  FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_lenovo_accessory_cmd_set_command_id(lenovo_hid_cmd,
					       FU_LENOVO_ACCESSORY_DFU_ID_DFU_ENTRY |
						   (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));

	if (!fu_lenovo_ble_data_set_cmd(lenovo_ble_data, lenovo_hid_cmd, error))
		return FALSE;
	return fu_lenovo_accessory_ble_command_process(ble_device,
						       lenovo_ble_data->buf,
						       FU_IOCTL_FLAG_RETRY,
						       error);
}

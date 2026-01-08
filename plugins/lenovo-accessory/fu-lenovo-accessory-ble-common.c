/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-accessory-ble-common.h"
#include "fu-lenovo-accessory-struct.h"

#define UUID_WRITE "c1d02501-2d1f-400a-95d2-6a2f7bca0c25"
#define UUID_READ  "c1d02502-2d1f-400a-95d2-6a2f7bca0c25"

static gboolean
fu_lenovo_accessory_ble_poll_cb(FuDevice *device, gpointer user_data, GError **error)
{
	GByteArray *buffer = (GByteArray *)user_data;
	FuLenovoStatus status;
	g_autoptr(GByteArray) res = NULL;

	res = fu_bluez_device_read(FU_BLUEZ_DEVICE(device), UUID_READ, error);
	if (res == NULL)
		return FALSE;
	if (res->len == 0) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_READ, "received empty data");
		return FALSE;
	}
	status = res->data[0] & 0x0F;
	if (status == FU_LENOVO_STATUS_COMMAND_BUSY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "command busy");
		return FALSE;
	}
	if (status != FU_LENOVO_STATUS_COMMAND_SUCCESSFUL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "command failed: 0x%02x",
			    status);
		return FALSE;
	}

	/* success */
	g_byte_array_set_size(buffer, 0);
	g_byte_array_append(buffer, res->data, res->len);
	return TRUE;
}

static gboolean
fu_lenovo_accessory_ble_process(FuBluezDevice *ble_device,
				GByteArray *buffer,
				FuIoctlFlags flags,
				GError **error)
{
	if (!fu_bluez_device_write(ble_device, UUID_WRITE, buffer, error)) {
		g_prefix_error_literal(error, "failed to write cmd: ");
		return FALSE;
	}
	return fu_device_retry_full(FU_DEVICE(ble_device),
				    fu_lenovo_accessory_ble_poll_cb,
				    50,	    /* count */
				    10,	    /* delay in ms */
				    buffer, /* user_data */
				    error);
}

gboolean
fu_lenovo_accessory_ble_fwversion(FuBluezDevice *ble_device,
				  guint8 *major,
				  guint8 *minor,
				  guint8 *micro,
				  GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoBleFwVersion) st_fwversion = fu_struct_lenovo_ble_fw_version_new();

	fu_struct_lenovo_accessory_cmd_set_target_status(st_cmd, 0x00);
	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x03);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DEVICE_INFORMATION);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_INFO_ID_FIRMWARE_VERSION |
		(FU_LENOVO_ACCESSORY_CMD_DIR_CMD_GET << 7));
	fu_struct_lenovo_accessory_cmd_set_flag_profile(st_cmd, 0x00);

	if (!fu_struct_lenovo_ble_fw_version_set_cmd(st_fwversion, st_cmd, error))
		return FALSE;
	if (!fu_lenovo_accessory_ble_process(ble_device,
					     st_fwversion->buf,
					     FU_IOCTL_FLAG_RETRY,
					     error))
		return FALSE;

	/* success */
	if (major != NULL)
		*major = fu_struct_lenovo_ble_fw_version_get_major(st_fwversion);
	if (minor != NULL)
		*minor = fu_struct_lenovo_ble_fw_version_get_minor(st_fwversion);
	if (micro != NULL)
		*micro = fu_struct_lenovo_ble_fw_version_get_internal(st_fwversion);
	return TRUE;
}

gboolean
fu_lenovo_accessory_ble_get_devicemode(FuBluezDevice *ble_device, guint8 *mode, GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoBleDevicemode) st_mode = fu_struct_lenovo_ble_devicemode_new();

	fu_struct_lenovo_accessory_cmd_set_target_status(st_cmd, 0x00);
	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x01);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DEVICE_INFORMATION);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_INFO_ID_DEVICE_MODE | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));

	if (!fu_struct_lenovo_ble_devicemode_set_cmd(st_mode, st_cmd, error))
		return FALSE;
	if (!fu_lenovo_accessory_ble_process(ble_device,
					     st_mode->buf,
					     FU_IOCTL_FLAG_RETRY,
					     error)) {
		return FALSE;
	}

	/* success */
	*mode = fu_struct_lenovo_ble_devicemode_get_mode(st_mode);
	return TRUE;
}

gboolean
fu_lenovo_accessory_ble_dfu_set_devicemode(FuBluezDevice *ble_device, guint8 mode, GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoBleData) st_data = fu_struct_lenovo_ble_data_new();

	fu_struct_lenovo_accessory_cmd_set_target_status(st_cmd, 0x00);
	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x01);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DEVICE_INFORMATION);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_INFO_ID_DEVICE_MODE | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));

	if (!fu_struct_lenovo_ble_data_set_cmd(st_data, st_cmd, error))
		return FALSE;
	if (!fu_struct_lenovo_ble_data_set_data(st_data, &mode, 1, error))
		return FALSE;
	if (mode == 0x02)
		return fu_bluez_device_write(ble_device, UUID_WRITE, st_data->buf, error);
	return fu_lenovo_accessory_ble_process(ble_device,
					       st_data->buf,
					       FU_IOCTL_FLAG_RETRY,
					       error);
}

gboolean
fu_lenovo_accessory_ble_dfu_exit(FuBluezDevice *ble_device, guint8 exit_code, GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoBleData) st_data = fu_struct_lenovo_ble_data_new();

	fu_struct_lenovo_accessory_cmd_set_target_status(st_cmd, 0x00);
	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x01);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_DFU_ID_DFU_EXIT | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));

	if (!fu_struct_lenovo_ble_data_set_cmd(st_data, st_cmd, error))
		return FALSE;
	if (!fu_struct_lenovo_ble_data_set_data(st_data, &exit_code, 1, error))
		return FALSE;
	if (!fu_bluez_device_write(ble_device, UUID_WRITE, st_data->buf, error)) {
		g_prefix_error_literal(error, "failed to write cmd: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
fu_lenovo_accessory_ble_dfu_attribute(FuBluezDevice *ble_device,
				      guint8 *major_ver,
				      guint8 *minor_ver,
				      guint16 *product_pid,
				      guint8 *processor_id,
				      guint32 *app_max_size,
				      guint32 *page_size,
				      GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoBleDfuAttribute) st_attribute =
	    fu_struct_lenovo_ble_dfu_attribute_new();

	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x0D);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_DFU_ID_DFU_ATTRIBUTE | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_GET << 7));

	if (!fu_struct_lenovo_ble_dfu_attribute_set_cmd(st_attribute, st_cmd, error))
		return FALSE;
	if (!fu_lenovo_accessory_ble_process(ble_device,
					     st_attribute->buf,
					     FU_IOCTL_FLAG_RETRY,
					     error))
		return FALSE;

	/* success */
	if (major_ver != NULL)
		*major_ver = fu_struct_lenovo_ble_dfu_attribute_get_major_ver(st_attribute);
	if (minor_ver != NULL)
		*minor_ver = fu_struct_lenovo_ble_dfu_attribute_get_minor_ver(st_attribute);
	if (product_pid != NULL)
		*product_pid = fu_struct_lenovo_ble_dfu_attribute_get_product_pid(st_attribute);
	if (processor_id != NULL)
		*processor_id = fu_struct_lenovo_ble_dfu_attribute_get_processor_id(st_attribute);
	if (app_max_size != NULL) {
		*app_max_size = fu_struct_lenovo_ble_dfu_attribute_get_app_max_size(st_attribute);
	}
	if (page_size != NULL)
		*page_size = fu_struct_lenovo_ble_dfu_attribute_get_page_size(st_attribute);
	return TRUE;
}

gboolean
fu_lenovo_accessory_ble_dfu_prepare(FuBluezDevice *ble_device,
				    guint8 file_type,
				    guint32 start_address,
				    guint32 end_address,
				    guint32 crc32,
				    GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoBleDfuPrepare) st_prepare = fu_struct_lenovo_ble_dfu_prepare_new();

	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x0D);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_DFU_ID_DFU_PREPARE | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));

	if (!fu_struct_lenovo_ble_dfu_prepare_set_cmd(st_prepare, st_cmd, error))
		return FALSE;
	fu_struct_lenovo_ble_dfu_prepare_set_file_type(st_prepare, file_type);
	fu_struct_lenovo_ble_dfu_prepare_set_start_address(st_prepare, start_address);
	fu_struct_lenovo_ble_dfu_prepare_set_end_address(st_prepare, end_address);
	fu_struct_lenovo_ble_dfu_prepare_set_crc32(st_prepare, crc32);
	return fu_lenovo_accessory_ble_process(ble_device,
					       st_prepare->buf,
					       FU_IOCTL_FLAG_RETRY,
					       error);
}

gboolean
fu_lenovo_accessory_ble_dfu_file(FuBluezDevice *ble_device,
				 guint8 file_type,
				 guint32 address,
				 const guint8 *buf,
				 gsize bufsz,
				 GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoBleDfuFw) st_file = fu_struct_lenovo_ble_dfu_fw_new();

	fu_struct_lenovo_accessory_cmd_set_target_status(st_cmd, 0x00);
	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, bufsz + 5);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_DFU_ID_DFU_FILE | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));

	if (!fu_struct_lenovo_ble_dfu_fw_set_cmd(st_file, st_cmd, error))
		return FALSE;
	fu_struct_lenovo_ble_dfu_fw_set_file_type(st_file, file_type);
	fu_struct_lenovo_ble_dfu_fw_set_offset_address(st_file, address);
	if (!fu_struct_lenovo_ble_dfu_fw_set_data(st_file, buf, bufsz, error))
		return FALSE;
	return fu_lenovo_accessory_ble_process(ble_device,
					       st_file->buf,
					       FU_IOCTL_FLAG_RETRY,
					       error);
}

gboolean
fu_lenovo_accessory_ble_dfu_crc(FuBluezDevice *ble_device, guint32 *crc32, GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoBleDfuCrc) st_crc = fu_struct_lenovo_ble_dfu_crc_new();

	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x05);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_DFU_ID_DFU_CRC | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_GET << 7));

	if (!fu_struct_lenovo_ble_dfu_crc_set_cmd(st_crc, st_cmd, error))
		return FALSE;
	if (!fu_lenovo_accessory_ble_process(ble_device, st_crc->buf, FU_IOCTL_FLAG_RETRY, error))
		return FALSE;

	/* success */
	if (crc32 != NULL)
		*crc32 = fu_struct_lenovo_ble_dfu_crc_get_crc32(st_crc);
	return TRUE;
}

gboolean
fu_lenovo_accessory_ble_dfu_entry(FuBluezDevice *ble_device, GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoBleData) st_data = fu_struct_lenovo_ble_data_new();

	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_DFU_ID_DFU_ENTRY | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));

	if (!fu_struct_lenovo_ble_data_set_cmd(st_data, st_cmd, error))
		return FALSE;
	return fu_lenovo_accessory_ble_process(ble_device,
					       st_data->buf,
					       FU_IOCTL_FLAG_RETRY,
					       error);
}

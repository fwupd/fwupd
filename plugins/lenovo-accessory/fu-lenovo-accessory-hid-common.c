/*
 * Copyright 2026 Yuchao Li <liyc44@lenovo.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-lenovo-accessory-hid-common.h"
#include "fu-lenovo-accessory-struct.h"

static gboolean
fu_lenovo_accessory_hid_poll_cb(FuDevice *device, gpointer user_data, GError **error)
{
	GByteArray *buf = (GByteArray *)user_data;
	guint8 status;

	if (!fu_hidraw_device_get_feature(FU_HIDRAW_DEVICE(device),
					  buf->data,
					  buf->len,
					  FU_IOCTL_FLAG_NONE,
					  error))
		return FALSE;
	status = buf->data[1] & 0x0F;
	if (status == FU_LENOVO_STATUS_COMMAND_BUSY) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_BUSY, "command busy");
		return FALSE;
	}
	if (status != FU_LENOVO_STATUS_COMMAND_SUCCESSFUL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "command failed with status 0x%02x",
			    status);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_lenovo_accessory_hid_process(FuHidrawDevice *hidraw_device,
				GByteArray *buf,
				FuIoctlFlags flags,
				GError **error)
{
	if (!fu_hidraw_device_set_feature(hidraw_device, buf->data, buf->len, flags, error))
		return FALSE;
	return fu_device_retry_full(FU_DEVICE(hidraw_device),
				    fu_lenovo_accessory_hid_poll_cb,
				    5,
				    10,
				    buf,
				    error);
}

gboolean
fu_lenovo_accessory_hid_get_fwversion(FuHidrawDevice *hidraw_device,
				      guint8 *major,
				      guint8 *minor,
				      guint8 *micro,
				      GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoHidFwVersion) st_fwversion = fu_struct_lenovo_hid_fw_version_new();

	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x03);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DEVICE_INFORMATION);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_INFO_ID_FIRMWARE_VERSION |
		(FU_LENOVO_ACCESSORY_CMD_DIR_CMD_GET << 7));
	if (!fu_struct_lenovo_hid_fw_version_set_cmd(st_fwversion, st_cmd, error))
		return FALSE;
	if (!fu_lenovo_accessory_hid_process(hidraw_device,
					     st_fwversion->buf,
					     FU_IOCTL_FLAG_RETRY,
					     error)) {
		return FALSE;
	}
	if (major != NULL)
		*major = fu_struct_lenovo_hid_fw_version_get_major(st_fwversion);
	if (minor != NULL)
		*minor = fu_struct_lenovo_hid_fw_version_get_minor(st_fwversion);
	if (micro != NULL)
		*micro = fu_struct_lenovo_hid_fw_version_get_internal(st_fwversion);
	return TRUE;
}

gboolean
fu_lenovo_accessory_hid_set_mode(FuHidrawDevice *hidraw_device, guint8 mode, GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoHidDevicemode) lenovo_hid_mode =
	    fu_struct_lenovo_hid_devicemode_new();

	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x01);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DEVICE_INFORMATION);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_INFO_ID_DEVICE_MODE | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));
	if (!fu_struct_lenovo_hid_devicemode_set_cmd(lenovo_hid_mode, st_cmd, error))
		return FALSE;
	fu_struct_lenovo_hid_devicemode_set_mode(lenovo_hid_mode, mode);
	if (mode == 0x02) {
		return (fu_hidraw_device_set_feature(hidraw_device,
						     lenovo_hid_mode->buf->data,
						     lenovo_hid_mode->buf->len,
						     FU_IOCTL_FLAG_NONE,
						     error));
	} else {
		return fu_lenovo_accessory_hid_process(hidraw_device,
						       lenovo_hid_mode->buf,
						       FU_IOCTL_FLAG_RETRY,
						       error);
	}
}

/* @exit_code: the exit status code (e.g., 0x00 for success/reboot) */
gboolean
fu_lenovo_accessory_hid_dfu_exit(FuHidrawDevice *hidraw_device, guint8 exit_code, GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoHidDfuExit) lenovo_hid_dfuexit =
	    fu_struct_lenovo_hid_dfu_exit_new();
	g_autoptr(GError) error_local = NULL;

	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_DFU_ID_DFU_EXIT | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));
	if (!fu_struct_lenovo_hid_dfu_exit_set_cmd(lenovo_hid_dfuexit, st_cmd, error))
		return FALSE;

	/*
	 * The device performs an immediate reset/reboot as soon as it receives the
	 * DFU_EXIT command and therefore never sends back an ACK. The resulting
	 * error (e.g., -EPIPE or -EIO) is expected and indicates that the reboot
	 * was successfully triggered.
	 */
	if (!fu_hidraw_device_set_feature(hidraw_device,
					  lenovo_hid_dfuexit->buf->data,
					  lenovo_hid_dfuexit->buf->len,
					  FU_IOCTL_FLAG_NONE,
					  &error_local)) {
		g_debug("ignoring: %s", error_local->message);
	}

	/* success */
	return TRUE;
}

gboolean
fu_lenovo_accessory_hid_dfu_attribute(FuHidrawDevice *hidraw_device,
				      guint8 *major_ver,
				      guint8 *minor_ver,
				      guint16 *product_pid,
				      guint8 *processor_id,
				      guint32 *app_max_size,
				      guint32 *page_size,
				      GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoHidDfuAttribute) lenovo_hid_attribute =
	    fu_struct_lenovo_hid_dfu_attribute_new();

	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x0D);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_DFU_ID_DFU_ATTRIBUTE | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_GET << 7));
	if (!fu_struct_lenovo_hid_dfu_attribute_set_cmd(lenovo_hid_attribute, st_cmd, error))
		return FALSE;
	if (!fu_lenovo_accessory_hid_process(hidraw_device,
					     lenovo_hid_attribute->buf,
					     FU_IOCTL_FLAG_RETRY,
					     error))
		return FALSE;

	/* success */
	if (major_ver != NULL)
		*major_ver = fu_struct_lenovo_hid_dfu_attribute_get_major_ver(lenovo_hid_attribute);
	if (minor_ver != NULL)
		*minor_ver = fu_struct_lenovo_hid_dfu_attribute_get_minor_ver(lenovo_hid_attribute);
	if (product_pid != NULL)
		*product_pid =
		    fu_struct_lenovo_hid_dfu_attribute_get_product_pid(lenovo_hid_attribute);
	if (processor_id != NULL)
		*processor_id =
		    fu_struct_lenovo_hid_dfu_attribute_get_processor_id(lenovo_hid_attribute);
	if (app_max_size != NULL)
		*app_max_size =
		    fu_struct_lenovo_hid_dfu_attribute_get_app_max_size(lenovo_hid_attribute);
	if (page_size != NULL)
		*page_size = fu_struct_lenovo_hid_dfu_attribute_get_page_size(lenovo_hid_attribute);
	return TRUE;
}

gboolean
fu_lenovo_accessory_hid_dfu_prepare(FuHidrawDevice *hidraw_device,
				    guint8 file_type,
				    guint32 start_address,
				    guint32 end_address,
				    guint32 crc32,
				    GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoHidDfuPrepare) lenovo_hid_prepare =
	    fu_struct_lenovo_hid_dfu_prepare_new();

	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, 0x0D);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_DFU_ID_DFU_PREPARE | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));
	if (!fu_struct_lenovo_hid_dfu_prepare_set_cmd(lenovo_hid_prepare, st_cmd, error))
		return FALSE;
	fu_struct_lenovo_hid_dfu_prepare_set_file_type(lenovo_hid_prepare, file_type);
	fu_struct_lenovo_hid_dfu_prepare_set_start_address(lenovo_hid_prepare, start_address);
	fu_struct_lenovo_hid_dfu_prepare_set_end_address(lenovo_hid_prepare, end_address);
	fu_struct_lenovo_hid_dfu_prepare_set_crc32(lenovo_hid_prepare, crc32);
	return fu_lenovo_accessory_hid_process(hidraw_device,
					       lenovo_hid_prepare->buf,
					       FU_IOCTL_FLAG_RETRY,
					       error);
}

gboolean
fu_lenovo_accessory_hid_dfu_file(FuHidrawDevice *hidraw_device,
				 guint8 file_type,
				 guint32 address,
				 const guint8 *file_data,
				 guint8 block_size,
				 GError **error)
{
	g_autoptr(FuStructLenovoAccessoryCmd) st_cmd = fu_struct_lenovo_accessory_cmd_new();
	g_autoptr(FuStructLenovoHidDfuFw) lenovo_hid_fw = fu_struct_lenovo_hid_dfu_fw_new();

	fu_struct_lenovo_accessory_cmd_set_data_size(st_cmd, block_size + 5);
	fu_struct_lenovo_accessory_cmd_set_command_class(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_COMMAND_CLASS_DFU_CLASS);
	fu_struct_lenovo_accessory_cmd_set_command_id(
	    st_cmd,
	    FU_LENOVO_ACCESSORY_DFU_ID_DFU_FILE | (FU_LENOVO_ACCESSORY_CMD_DIR_CMD_SET << 7));
	if (!fu_struct_lenovo_hid_dfu_fw_set_cmd(lenovo_hid_fw, st_cmd, error))
		return FALSE;
	fu_struct_lenovo_hid_dfu_fw_set_file_type(lenovo_hid_fw, file_type);
	fu_struct_lenovo_hid_dfu_fw_set_offset_address(lenovo_hid_fw, address);
	if (!fu_struct_lenovo_hid_dfu_fw_set_data(lenovo_hid_fw, file_data, block_size, error))
		return FALSE;
	return fu_lenovo_accessory_hid_process(hidraw_device,
					       lenovo_hid_fw->buf,
					       FU_IOCTL_FLAG_RETRY,
					       error);
}

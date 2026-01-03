#include "config.h"

#include "fu-lenovo-accessory-command.h"
#include "fu-lenovo-accessory-struct.h"

gboolean
fu_lenovo_accessory_command_process(FuHidrawDevice *hidraw_device,
				    guint8 *req,
				    gsize req_sz,
				    FuIoctlFlags flags,
				    GError **error)
{
	g_autofree guint8 *rsp = g_malloc0(req_sz); /* 自动 free，内容已清零 */
	guint retries = 5;
	guint8 status = 0;
	/* 下发命令 */
	if (!fu_hidraw_device_set_feature(hidraw_device, req, req_sz, flags, error))
		return FALSE;

	/* 轮询状态 */
	while (retries--) {
		if (!fu_hidraw_device_get_feature(hidraw_device, rsp, req_sz, flags, error))
			return FALSE;

		status = rsp[1] & 0x0F;
		if (status == 0x02) /* success */
		{
			if (!fu_memcpy_safe(req, req_sz, 0, rsp, req_sz, 0, req_sz, error))
				return FALSE;
			return TRUE;
		}
		if (status != 0x01) { /* 既不是 busy 也不是 success → 出错 */
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_WRITE,
				    "command failed with status 0x%02x",
				    status);
			return FALSE;
		}
		g_usleep(10 * 1000); /* 10 ms 退避，避免总线狂飙 */
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_WRITE,
			    "command timeout (device always busy)");
	return FALSE;
}

gboolean
fu_lenovo_accessory_command_fwversion(FuHidrawDevice *hidraw_device,
				      guint8 *major,
				      guint8 *minor,
				      guint8 *internal,
				      GError **error)
{
	g_autofree guint8 *data = g_new0(guint8, 65);
	memset(data, 0, 65);
	data[FU_LENOVO_HID_DATA_HEADER_DATA_SIZE] = 0x03;
	data[FU_LENOVO_HID_DATA_HEADER_COMMAND_CLASS] = 0x00;
	data[FU_LENOVO_HID_DATA_HEADER_COMMAND_ID] = 0x81;
	if (!fu_lenovo_accessory_command_process(hidraw_device,
						 data,
						 65,
						 FU_IOCTL_FLAG_RETRY,
						 error)) {
		return FALSE;
	}
	*major = data[FU_LENOVO_HID_DATA_HEADER_PAYLOAD_BASE];
	*minor = data[FU_LENOVO_HID_DATA_HEADER_PAYLOAD_BASE + 1];
	*internal = data[FU_LENOVO_HID_DATA_HEADER_PAYLOAD_BASE + 2];
	return TRUE;
}

gboolean
fu_lenovo_accessory_command_dfu_set_devicemode(FuHidrawDevice *hidraw_device,
					       guint8 mode,
					       GError **error)
{
	g_autofree guint8 *data = g_new0(guint8, 65);
	memset(data, 0, 65);
	data[FU_LENOVO_HID_DATA_HEADER_DATA_SIZE] = 0x01;
	data[FU_LENOVO_HID_DATA_HEADER_COMMAND_CLASS] = 0x00;
	data[FU_LENOVO_HID_DATA_HEADER_COMMAND_ID] = 0x04;
	data[FU_LENOVO_HID_DATA_HEADER_PAYLOAD_BASE] = mode;
	return fu_lenovo_accessory_command_process(hidraw_device,
						   data,
						   65,
						   FU_IOCTL_FLAG_RETRY,
						   error);
}

gboolean
fu_lenovo_accessory_command_dfu_exit(FuHidrawDevice *hidraw_device,
				     guint8 exit_code,
				     GError **error)
{
	g_autofree guint8 *data = g_new0(guint8, 65);
	memset(data, 0, 65);
	data[FU_LENOVO_HID_DATA_HEADER_DATA_SIZE] = 0x00;
	data[FU_LENOVO_HID_DATA_HEADER_COMMAND_CLASS] = 0x09;
	data[FU_LENOVO_HID_DATA_HEADER_COMMAND_ID] = 0x05;
	data[FU_LENOVO_HID_DATA_HEADER_PAYLOAD_BASE] = exit_code;
	return fu_lenovo_accessory_command_process(hidraw_device,
						   data,
						   65,
						   FU_IOCTL_FLAG_RETRY,
						   error);
}

gboolean
fu_lenovo_accessory_command_dfu_attribute(FuHidrawDevice *hidraw_device,
					  guint8 *major_ver,
					  guint8 *minor_ver,
					  guint16 *product_pid,
					  guint8 *processor_id,
					  guint32 *app_max_size,
					  guint32 *page_size,
					  GError **error)
{
	g_autofree guint8 *data = g_new0(guint8, 65);
	memset(data, 0, 65);
	data[FU_LENOVO_HID_DATA_HEADER_DATA_SIZE] = 0x0D;
	data[FU_LENOVO_HID_DATA_HEADER_COMMAND_CLASS] = 0x09;
	data[FU_LENOVO_HID_DATA_HEADER_COMMAND_ID] = 0x81;
	if (!fu_lenovo_accessory_command_process(hidraw_device,
						 data,
						 65,
						 FU_IOCTL_FLAG_RETRY,
						 error)) {
		return FALSE;
	}
	if (major_ver != NULL)
		*major_ver = data[FU_LENOVO_HID_DATA_HEADER_PAYLOAD_BASE];
	if (minor_ver != NULL)
		*minor_ver = data[FU_LENOVO_HID_DATA_HEADER_PAYLOAD_BASE + 1];
	if (product_pid != NULL)
		*product_pid = fu_memread_uint16(data + FU_LENOVO_HID_DATA_HEADER_PAYLOAD_BASE + 2,
						 G_BIG_ENDIAN);
	if (processor_id != NULL)
		*processor_id = data[FU_LENOVO_HID_DATA_HEADER_PAYLOAD_BASE + 4];
	if (app_max_size != NULL)
		*app_max_size = fu_memread_uint32(data + FU_LENOVO_HID_DATA_HEADER_PAYLOAD_BASE + 5,
						  G_BIG_ENDIAN);
	if (page_size != NULL)
		*page_size = fu_memread_uint32(data + FU_LENOVO_HID_DATA_HEADER_PAYLOAD_BASE + 9,
					       G_BIG_ENDIAN);
	return TRUE;
}

gboolean
fu_lenovo_accessory_command_dfu_prepare(FuHidrawDevice *hidraw_device,
					guint8 file_type,
					guint32 start_address,
					guint32 end_address,
					guint32 crc32,
					GError **error)
{
	g_autofree guint8 *data = g_new0(guint8, 65);
	guint8 *pld = &data[FU_LENOVO_HID_DATA_HEADER_PAYLOAD_BASE];
	memset(data, 0, 65);
	data[FU_LENOVO_HID_DATA_HEADER_DATA_SIZE] = 0x0D;
	data[FU_LENOVO_HID_DATA_HEADER_COMMAND_CLASS] = 0x09;
	data[FU_LENOVO_HID_DATA_HEADER_COMMAND_ID] = 0x02;
	data[FU_LENOVO_HID_DATA_HEADER_PAYLOAD_BASE] = file_type;
	fu_memwrite_uint32(pld + 0, start_address, G_BIG_ENDIAN);
	fu_memwrite_uint32(pld + 4, end_address, G_BIG_ENDIAN);
	fu_memwrite_uint32(pld + 8, crc32, G_BIG_ENDIAN);
	return fu_lenovo_accessory_command_process(hidraw_device,
						   data,
						   65,
						   FU_IOCTL_FLAG_RETRY,
						   error);
}

gboolean
fu_lenovo_accessory_command_dfu_file(FuHidrawDevice *hidraw_device,
				     guint8 file_type,
				     guint32 address,
				     guint8 *file_data,
				     guint8 block_size,
				     GError **error)
{
	g_autofree guint8 *data = g_new0(guint8, 65);
	guint8 *pld = &data[FU_LENOVO_HID_DATA_HEADER_PAYLOAD_BASE];
	memset(data, 0, 65);
	data[FU_LENOVO_HID_DATA_HEADER_DATA_SIZE] = block_size + 5;
	data[FU_LENOVO_HID_DATA_HEADER_COMMAND_CLASS] = 0x09;
	data[FU_LENOVO_HID_DATA_HEADER_COMMAND_ID] = 0x03;
	data[FU_LENOVO_HID_DATA_HEADER_PAYLOAD_BASE] = file_type;
	fu_memwrite_uint32(pld + 1, address, G_BIG_ENDIAN);
	if (!fu_memcpy_safe(pld + 5, 65, 0, file_data, 65, 0, block_size, error))
		return FALSE;
	return fu_lenovo_accessory_command_process(hidraw_device,
						   data,
						   65,
						   FU_IOCTL_FLAG_RETRY,
						   error);
}

/*
 * Copyright 2025 lazro <li@shzj.cc>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "config.h"

#include "fu-legion-go2-device.h"
#include "fu-legion-go2-struct.h"

#define FU_LEGION_GO2_DEVICE_VID       0x17EF
#define FU_LEGION_GO2_DEVICE_PID_BEGIN 0x61EB
#define FU_LEGION_GO2_DEVICE_PID_END   0x61EE

#define FU_LEGION_GO2_DEVICE_IO_TIMEOUT            500
#define FU_LEGION_GO2_DEVICE_BUSY_WAIT_TIMEOUT     (60*1000)
#define FU_LEGION_GO2_DEVICE_READ_RESPONSE_TIMEOUT 2000

#define FU_LEGION_GO2_DEVICE_FW_SIGNED_LENGTH 384
#define FU_LEGION_GO2_DEVICE_FW_ID_LENGTH     4
#define FU_LEGION_GO2_DEVICE_FW_PACKET_LENGTH 32
#define FU_LEGION_GO2_DEVICE_FW_REPORT_LENGTH 64


struct _FuLegionGo2Device {
	FuHidrawDevice parent_instance;
};

G_DEFINE_TYPE(FuLegionGo2Device, fu_legion_go2_device, FU_TYPE_HIDRAW_DEVICE)

static gboolean
fu_legion_go2_device_read_response(FuLegionGo2Device *self, guint8 main_id, guint8 sub_id, GByteArray* res)
{
	gint64 timeNow = g_get_monotonic_time();
	while (TRUE)
	{
		gint64 timeNext = g_get_monotonic_time();
		gint64 interval = (timeNext - timeNow) / 1000;
		if (interval > FU_LEGION_GO2_DEVICE_READ_RESPONSE_TIMEOUT)
		{
			g_message("read response timeout: ");
			break;
		}
		if (!fu_udev_device_read(FU_UDEV_DEVICE(self),
						res->data,
						res->len,
						NULL,
						FU_LEGION_GO2_DEVICE_IO_TIMEOUT,
						FU_IO_CHANNEL_FLAG_NONE,
						NULL)) {
			g_message("execute fu_udev_device_read failed: ");
			continue;
		}
		if (res->data[2] == main_id &&
		    res->data[3] == sub_id)
		{
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean
fu_legion_go2_device_read_response_ex(FuLegionGo2Device *self, guint8 main_id, guint8 sub_id, guint8 id, GByteArray* res)
{
	gint64 timeNow = g_get_monotonic_time();
	while (TRUE)
	{
		gint64 timeNext = g_get_monotonic_time();
		gint64 interval = (timeNext - timeNow) / 1000;
		if (interval > FU_LEGION_GO2_DEVICE_READ_RESPONSE_TIMEOUT)
		{
			g_message("read response timeout: ");
			break;
		}
		if (!fu_udev_device_read(FU_UDEV_DEVICE(self),
						res->data,
						res->len,
						NULL,
						FU_LEGION_GO2_DEVICE_IO_TIMEOUT,
						FU_IO_CHANNEL_FLAG_NONE,
						NULL)) {
			g_message("execute fu_udev_device_read failed: ");
			continue;
		}
		if (res->data[2] == main_id &&
		    res->data[3] == sub_id &&
		    res->data[4] ==  id)
		{
			return TRUE;
		}
	}

	return FALSE;
}

static gboolean
fu_legion_go2_device_upgrade_start(FuLegionGo2Device *self, GByteArray *req, GByteArray* res, GError **error)
{
	if (req != NULL) {
		if (!fu_udev_device_write(FU_UDEV_DEVICE(self),
					  req->data,
					  req->len,
					  FU_LEGION_GO2_DEVICE_IO_TIMEOUT,
					  FU_IO_CHANNEL_FLAG_NONE,
					  error)) {
			g_message("send start command failed: ");
			return FALSE;
		}
	}
	if (res != NULL) {
		if (!fu_legion_go2_device_read_response(self, 0x53, 0x11, res))
		{
			g_message("read start response failed: ");
			return FALSE;
		}

		if (res->data[7] != FU_LEGION_GO2_UPGRADE_STEP_START)
		{
			g_message("response mismatch start command: ");
			return FALSE;
		}

		guint8 status = (guint8)res->data[9];
		gint64 timeNow = g_get_monotonic_time();
		while (status == FU_LEGION_GO2_RESPONSE_STATUS_BUSY)
		{
			gint64 timeNext = g_get_monotonic_time();
			gint64 interval = (timeNext - timeNow) / 1000;
			if (interval > FU_LEGION_GO2_DEVICE_BUSY_WAIT_TIMEOUT)
			{
				g_message("start busy wait timeout: ");
				return FALSE;
			}
			if (!fu_legion_go2_device_read_response(self, 0x53, 0x11, res))
			{
				g_message("read start response failed again: ");
				continue;
			}
			status = res->data[9];
		}

		if (status != FU_LEGION_GO2_RESPONSE_STATUS_OK)
		{
			g_message("failed to execute start step: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_legion_go2_device_upgrade_query_size(FuLegionGo2Device *self, GByteArray *req, GByteArray *res, GError **error)
{
	if (req != NULL) {
		if (!fu_udev_device_write(FU_UDEV_DEVICE(self),
					  req->data,
					  req->len,
					  FU_LEGION_GO2_DEVICE_IO_TIMEOUT,
					  FU_IO_CHANNEL_FLAG_NONE,
					  error)) {
			g_message("send query size command failed: ");
			return FALSE;
		}
	}
	if (res != NULL) {
		if (!fu_legion_go2_device_read_response(self, 0x53, 0x11, res))
		{
			g_message("read query size response failed: ");
			return FALSE;
		}

		if (res->data[7] != FU_LEGION_GO2_UPGRADE_STEP_QUERY_SIZE)
		{
			g_message("response mismatch query size command: ");
			return FALSE;
		}

		guint8 status = (guint8)res->data[9];
		gint64 timeNow = g_get_monotonic_time();
		while (status == FU_LEGION_GO2_RESPONSE_STATUS_BUSY)
		{
			gint64 timeNext = g_get_monotonic_time();
			gint64 interval = (timeNext - timeNow) / 1000;
			if (interval > FU_LEGION_GO2_DEVICE_BUSY_WAIT_TIMEOUT)
			{
				g_message("query size busy wait timeout: ");
				return FALSE;
			}
			if (!fu_legion_go2_device_read_response(self, 0x53, 0x11, res))
			{
				g_message("read query size response failed again: ");
				continue;
			}
			status = res->data[9];
		}

		if (status == FU_LEGION_GO2_RESPONSE_STATUS_FAIL)
		{
			g_message("failed to execute query size step: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_legion_go2_device_upgrade_write_data(FuLegionGo2Device *self, GByteArray *req, GByteArray *res, gboolean wait, GError **error)
{
	if (req != NULL) {
		if (!fu_udev_device_write(FU_UDEV_DEVICE(self),
					  req->data,
					  req->len,
					  FU_LEGION_GO2_DEVICE_IO_TIMEOUT,
					  FU_IO_CHANNEL_FLAG_NONE,
					  error)) {
			g_message("send firmware data command failed: ");
			return FALSE;
		}
	}

	if (!wait)
	{
		return TRUE;
	}

	if (res != NULL) {
		if (!fu_legion_go2_device_read_response(self, 0x53, 0x11, res))
		{
			g_message("read firmware data response failed: ");
			return FALSE;
		}

		if (res->data[7] != FU_LEGION_GO2_UPGRADE_STEP_WRITE_DATA)
		{
			g_message("response mismatch firmware data command: ");
			return FALSE;
		}

		guint8 status = (guint8)res->data[9];
		gint64 timeNow = g_get_monotonic_time();
		while (status == FU_LEGION_GO2_RESPONSE_STATUS_BUSY)
		{
			gint64 timeNext = g_get_monotonic_time();
			gint64 interval = (timeNext - timeNow) / 1000;
			if (interval > FU_LEGION_GO2_DEVICE_BUSY_WAIT_TIMEOUT)
			{
				g_message("firmware data busy wait timeout: ");
				return FALSE;
			}
			if (!fu_legion_go2_device_read_response(self, 0x53, 0x11, res))
			{
				g_message("read firmware data response failed again: ");
				continue;
			}
			status = res->data[9];
		}

		if (status != FU_LEGION_GO2_RESPONSE_STATUS_OK)
		{
			g_message("failed to execute firmware data step: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_legion_go2_device_upgrade_verify(FuLegionGo2Device *self, GByteArray *req, GByteArray *res, GError **error)
{
	if (req != NULL) {
		if (!fu_udev_device_write(FU_UDEV_DEVICE(self),
					  req->data,
					  req->len,
					  FU_LEGION_GO2_DEVICE_IO_TIMEOUT,
					  FU_IO_CHANNEL_FLAG_NONE,
					  error)) {
			g_message("send verify command failed: ");
			return FALSE;
		}
	}
	if (res != NULL) {
		if (!fu_legion_go2_device_read_response(self, 0x53, 0x11, res))
		{
			g_message("read verify response failed: ");
			return FALSE;
		}

		if (res->data[7] != FU_LEGION_GO2_UPGRADE_STEP_VERIFY)
		{
			g_message("response mismatch verify command: ");
			return FALSE;
		}

		guint8 status = (guint8)res->data[9];
		gint64 timeNow = g_get_monotonic_time();
		while (status == FU_LEGION_GO2_RESPONSE_STATUS_BUSY)
		{
			gint64 timeNext = g_get_monotonic_time();
			gint64 interval = (timeNext - timeNow) / 1000;
			if (interval > FU_LEGION_GO2_DEVICE_BUSY_WAIT_TIMEOUT)
			{
				g_message("verify busy wait timeout: ");
				return FALSE;
			}
			if (!fu_legion_go2_device_read_response(self, 0x53, 0x11, res))
			{
				g_message("read verify response failed again: ");
				continue;
			}
			status = res->data[9];
		}

		if (status != FU_LEGION_GO2_RESPONSE_STATUS_OK)
		{
			g_message("failed to execute verify step: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gchar*
fu_legion_go2_device_get_version(FuLegionGo2Device *self, guint8 id, GError **error)
{
	g_autoptr(FuStructLegionGo2NormalCmd) versionCmd = fu_struct_legion_go2_normal_cmd_new();
	guint8 content[] = { 
		0x01
	};
	fu_struct_legion_go2_normal_cmd_set_report_id(versionCmd, 5);
	fu_struct_legion_go2_normal_cmd_set_length(versionCmd, sizeof(content) + 4);
	fu_struct_legion_go2_normal_cmd_set_main_id(versionCmd, 0x79);
	fu_struct_legion_go2_normal_cmd_set_sub_id(versionCmd, 0x01);
	fu_struct_legion_go2_normal_cmd_set_device_id(versionCmd, id);
	fu_struct_legion_go2_normal_cmd_set_data(versionCmd, content, sizeof(content), error);

	if (!fu_udev_device_write(FU_UDEV_DEVICE(self),
				  versionCmd->buf->data,
				  versionCmd->buf->len,
				  FU_LEGION_GO2_DEVICE_IO_TIMEOUT,
				  FU_IO_CHANNEL_FLAG_NONE,
				  error)) {
		g_message("send version command failed: ");
		return NULL;
	}

	g_autoptr(GByteArray) response = g_byte_array_sized_new(FU_LEGION_GO2_DEVICE_FW_REPORT_LENGTH);
	g_byte_array_set_size(response, FU_LEGION_GO2_DEVICE_FW_REPORT_LENGTH);
	if (!fu_legion_go2_device_read_response_ex(self, 0x79, 0x01, id, response))
	{
		g_message("read version response failed: ");
		return NULL;
	}

	gchar* version = g_strdup_printf("%x%02x%02x%02x", 
					  response->data[13], 
					  response->data[14], 
					  response->data[15], 
					  response->data[16]);
	g_message("device (%d) version: %s", id, version);
	
	return version;
}

static guint16
fu_legion_go2_device_calc_crc16(GByteArray* buffer)
{
	const guint16 POLY = 0x1021;
	guint16 crc = 0x0000;

	for (guint i = 0; i < buffer->len; i++)
	{
		crc ^= (guint16)(buffer->data[i] << 8);
		for (int j = 0; j < 8; j++)
		{
			if ((crc & 0x8000) != 0)
			{
				crc = (guint16)((crc << 1) ^ POLY);
			}
			else
			{
				crc = (guint16)(crc << 1);
			}
		}
	}

	return crc;
}

static guint8 fu_legion_go2_device_get_id(GByteArray* buffer)
{
	if (buffer->len < FU_LEGION_GO2_DEVICE_FW_SIGNED_LENGTH + FU_LEGION_GO2_DEVICE_FW_ID_LENGTH)
	{
		return 0;
	}

	guint offset = buffer->len - FU_LEGION_GO2_DEVICE_FW_SIGNED_LENGTH - FU_LEGION_GO2_DEVICE_FW_ID_LENGTH;
	for (guint i = 0; i < FU_LEGION_GO2_DEVICE_FW_ID_LENGTH; ++i)
	{
		guint8 id = buffer->data[offset + i];
		if (id == 2 || id == 7 || id == 8)
		{
			return id;
		}
	}

	return 0;
}

static gboolean
fu_legion_go2_device_probe(FuDevice *device, GError **error)
{
	guint16 vid = fu_device_get_vid(FU_DEVICE(device));
	guint16 pid = fu_device_get_pid(FU_DEVICE(device));

	if (vid == FU_LEGION_GO2_DEVICE_VID && 
	    pid >= FU_LEGION_GO2_DEVICE_PID_BEGIN && 
	    pid <= FU_LEGION_GO2_DEVICE_PID_END) {
		fu_device_set_name(device, "Legion Go2 MCU");
		fu_device_set_vendor(device, "Lenovo");
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_UPDATABLE);
		fu_device_add_protocol(device, "com.legion.go2");
		g_message("Detected Legion Go2 MCU device (vid=0x%04x pid=0x%04x)", vid, pid);
		return TRUE;
	}

	return FALSE;
}

static gboolean
fu_legion_go2_device_write_firmware(FuDevice *device,
                                    FuFirmware *firmware,
                                    FuProgress *progress,
                                    FwupdInstallFlags flags,
                                    GError **error)
{
	FuLegionGo2Device* self = FU_LEGION_GO2_DEVICE(device);

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 10, NULL);

	g_autoptr(GBytes) payload = fu_firmware_get_bytes(firmware, error);
	if (payload == NULL || g_bytes_get_size(payload) == 0)
	{
		g_message("firmware payload invalid: ");
		return FALSE;
	}

	gsize size = 0;
	const guint8 *data = g_bytes_get_data(payload, &size);
	g_autoptr(GByteArray) array = g_byte_array_sized_new(size);
	g_byte_array_append(array, data, size);

	guint8 id = fu_legion_go2_device_get_id(array);
	if (id == 0)
	{
		g_message("firmware device id invalid: ");
		return FALSE;
	}
	g_message("firmware device id: %d", id);

	guint16 crc16 = fu_legion_go2_device_calc_crc16(array);
	g_autoptr(GByteArray) response = g_byte_array_sized_new(FU_LEGION_GO2_DEVICE_FW_REPORT_LENGTH);
	g_byte_array_set_size(response, FU_LEGION_GO2_DEVICE_FW_REPORT_LENGTH);

	// start step
	{
		g_autoptr(FuStructLegionGo2UpgradeCmd) startCmd = fu_struct_legion_go2_upgrade_cmd_new();
		guint8 content[] = { 
			0x08,
			FU_LEGION_GO2_UPGRADE_STEP_START,
			0x00,
			crc16 >> 8 & 0xff,
			crc16 & 0xff,
			size >> 16 & 0xff,
			size >> 8 & 0xff,
			size & 0xff,
			0x01
		};
		fu_struct_legion_go2_upgrade_cmd_set_report_id(startCmd, 5);
		fu_struct_legion_go2_upgrade_cmd_set_length(startCmd, sizeof(content) + 5);
		fu_struct_legion_go2_upgrade_cmd_set_device_id(startCmd, id);
		fu_struct_legion_go2_upgrade_cmd_set_param(startCmd, 0x01);
		fu_struct_legion_go2_upgrade_cmd_set_data(startCmd, content, sizeof(content), error);
		if (!fu_legion_go2_device_upgrade_start(self, startCmd->buf, response, error))
		{
			return FALSE;
		}
	}
	fu_progress_step_done(progress);
	g_message("start step done: ");
	
	// query size step
	guint maxSize = 0;
	{
		g_autoptr(FuStructLegionGo2UpgradeCmd) queryCmd = fu_struct_legion_go2_upgrade_cmd_new();
		guint8 content[] = { 
			0x02,
			FU_LEGION_GO2_UPGRADE_STEP_QUERY_SIZE,
			0x01
		};
		fu_struct_legion_go2_upgrade_cmd_set_report_id(queryCmd, 5);
		fu_struct_legion_go2_upgrade_cmd_set_length(queryCmd, sizeof(content) + 5);
		fu_struct_legion_go2_upgrade_cmd_set_device_id(queryCmd, id);
		fu_struct_legion_go2_upgrade_cmd_set_param(queryCmd, 0x01);
		fu_struct_legion_go2_upgrade_cmd_set_data(queryCmd, content, sizeof(content), error);
		if (!fu_legion_go2_device_upgrade_query_size(self, queryCmd->buf, response, error))
		{
			return FALSE;
		}
		maxSize = response->data[9] << 8 | response->data[10];
	}
	fu_progress_step_done(progress);
	g_message("query size step done: ");

	// write data step
	{
		if (maxSize % FU_LEGION_GO2_DEVICE_FW_PACKET_LENGTH != 0)
		{
			g_message("max size (%u) invalid", maxSize);
			return FALSE;
		}
		guint innerLoopTimes = maxSize / FU_LEGION_GO2_DEVICE_FW_PACKET_LENGTH;
		guint outerLoopTimes = size / maxSize;
		if ((size % maxSize) != 0)
		{
			outerLoopTimes += 1;
		}
		guint sendSize = 0;
		for (guint i = 0; i < outerLoopTimes; ++i)
		{
			for (guint j = 0; j < innerLoopTimes; ++j)
			{
				if (sendSize >= size)
				{
					break;
				}
				g_autoptr(FuStructLegionGo2UpgradeCmd) writeCmd = fu_struct_legion_go2_upgrade_cmd_new();
				guint8 content[FU_LEGION_GO2_DEVICE_FW_PACKET_LENGTH + 1] = { 0 };
				memcpy(content, array->data + sendSize, FU_LEGION_GO2_DEVICE_FW_PACKET_LENGTH);
				content[FU_LEGION_GO2_DEVICE_FW_PACKET_LENGTH] = 0x01;
				fu_struct_legion_go2_upgrade_cmd_set_report_id(writeCmd, 5);
				fu_struct_legion_go2_upgrade_cmd_set_length(writeCmd, sizeof(content) + 5);
				fu_struct_legion_go2_upgrade_cmd_set_device_id(writeCmd, id);
				fu_struct_legion_go2_upgrade_cmd_set_param(writeCmd, 0x02);
				fu_struct_legion_go2_upgrade_cmd_set_data(writeCmd, content, sizeof(content), error);
				guint tempSize = sendSize + FU_LEGION_GO2_DEVICE_FW_PACKET_LENGTH;
				gboolean wait = (tempSize % maxSize == 0) || (tempSize >= size);
				if (!fu_legion_go2_device_upgrade_write_data(self, writeCmd->buf, response, wait, error))
				{
					return FALSE;
				}
				sendSize += FU_LEGION_GO2_DEVICE_FW_PACKET_LENGTH;
				if (wait)
				{
					guint recievedSize = response->data[10] << 24 | 
							     response->data[11] << 16 | 
							     response->data[12] << 8  | 
							     response->data[13];
					if (recievedSize != sendSize && recievedSize != size)
					{
						g_message("size verify failed(%u %u %u)", sendSize, recievedSize, size);
						return FALSE;
					}
				}
			}
		}
	}
	fu_progress_step_done(progress);
	g_message("write data step done: ");
	
	// verify step
	{
		g_autoptr(FuStructLegionGo2UpgradeCmd) verifyCmd = fu_struct_legion_go2_upgrade_cmd_new();
		guint8 content[] = { 
			0x02,
			FU_LEGION_GO2_UPGRADE_STEP_VERIFY,
			0x01
		};
		fu_struct_legion_go2_upgrade_cmd_set_report_id(verifyCmd, 5);
		fu_struct_legion_go2_upgrade_cmd_set_length(verifyCmd, sizeof(content) + 5);
		fu_struct_legion_go2_upgrade_cmd_set_device_id(verifyCmd, id);
		fu_struct_legion_go2_upgrade_cmd_set_param(verifyCmd, 0x01);
		fu_struct_legion_go2_upgrade_cmd_set_data(verifyCmd, content, sizeof(content), error);
		if (!fu_legion_go2_device_upgrade_verify(self, verifyCmd->buf, response, error))
		{
			return FALSE;
		}
	}
	fu_progress_step_done(progress);
	g_message("verify step done: ");

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
	g_autoptr(GPtrArray) imgs = NULL;

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
	g_autoptr(GError) error_touchpad = NULL;

	if (!fu_legion_go2_device_validate_descriptor(device, error))
		return FALSE;

	g_free(fu_legion_go2_device_get_version(FU_LEGION_GO2_DEVICE(device), 1, error));
	g_free(fu_legion_go2_device_get_version(FU_LEGION_GO2_DEVICE(device), 3, error));
	g_free(fu_legion_go2_device_get_version(FU_LEGION_GO2_DEVICE(device), 4, error));

	// set testing version
	fu_device_set_version(device, "1.0.0.0");
	return TRUE;
}

static void
fu_legion_go2_device_class_init(FuLegionGo2DeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_legion_go2_device_setup;
	device_class->probe = fu_legion_go2_device_probe;
	device_class->write_firmware = fu_legion_go2_device_write_firmware;
	device_class->set_progress = fu_legion_go2_device_set_progress;
}

static void
fu_legion_go2_device_init(FuLegionGo2Device *self)
{
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
}
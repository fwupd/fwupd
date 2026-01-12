/*
 * Copyright 2025 lazro <2059899519@qq.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-legion-hid-child.h"
#include "fu-legion-hid-device.h"
#include "fu-legion-hid-firmware.h"

struct _FuLegionHidDevice {
	FuHidrawDevice parent_instance;
};

G_DEFINE_TYPE(FuLegionHidDevice, fu_legion_hid_device, FU_TYPE_HIDRAW_DEVICE)

#define FU_LEGION_HID_DEVICE_IO_TIMEOUT	      500
#define FU_LEGION_HID_DEVICE_REBOOT_WAIT_TIME (10 * 1000)

#define FU_LEGION_HID_DEVICE_FW_SIGNED_LENGTH 384
#define FU_LEGION_HID_DEVICE_FW_ID_LENGTH     4
#define FU_LEGION_HID_DEVICE_FW_PACKET_LENGTH 32
#define FU_LEGION_HID_DEVICE_FW_REPORT_LENGTH 64

typedef struct {
	GByteArray *res;
	guint8 main_id;
	guint8 sub_id;
	FuLegionHidDeviceId dev_id;
	guint8 step;
} FuLegionHidRetryHelper;

static gboolean
fu_legion_hid_device_read_normal_response_retry_cb(FuDevice *device,
						   gpointer user_data,
						   GError **error)
{
	FuLegionHidRetryHelper *helper = (FuLegionHidRetryHelper *)user_data;
	GByteArray *res = helper->res;

	g_byte_array_set_size(res, FU_LEGION_HID_DEVICE_FW_REPORT_LENGTH);
	if (!fu_udev_device_read(FU_UDEV_DEVICE(device),
				 res->data,
				 res->len,
				 NULL,
				 FU_LEGION_HID_DEVICE_IO_TIMEOUT,
				 FU_IO_CHANNEL_FLAG_NONE,
				 error))
		return FALSE;
	if (res->data[2] != helper->main_id || res->data[3] != helper->sub_id ||
	    res->data[4] != helper->dev_id) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "response mismatch filter(%u, %u, %u), retrying...",
			    helper->main_id,
			    helper->sub_id,
			    helper->dev_id);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_legion_hid_device_read_response(FuLegionHidDevice *self,
				   FuLegionHidRetryHelper *helper,
				   GError **error)
{
	g_autoptr(GByteArray) res = g_byte_array_sized_new(FU_LEGION_HID_DEVICE_FW_REPORT_LENGTH);
	helper->res = res;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_legion_hid_device_read_normal_response_retry_cb,
				  120,
				  0,
				  helper,
				  error))
		return NULL;
	return g_steal_pointer(&res);
}

static gboolean
fu_legion_hid_device_check_upgrade_device_id(guint8 rsp_id, guint8 dev_id)
{
	if (rsp_id == dev_id)
		return TRUE;
	if (rsp_id == FU_LEGION_HID_DEVICE_ID_GAMEPAD_L &&
	    (dev_id == FU_LEGION_HID_DEVICE_ID_GAMEPAD_L2 ||
	     dev_id == FU_LEGION_HID_DEVICE_ID_GAMEPAD_L3))
		return TRUE;
	if (rsp_id == FU_LEGION_HID_DEVICE_ID_GAMEPAD_R &&
	    (dev_id == FU_LEGION_HID_DEVICE_ID_GAMEPAD_R2 ||
	     dev_id == FU_LEGION_HID_DEVICE_ID_GAMEPAD_R3))
		return TRUE;
	return FALSE;
}

static gboolean
fu_legion_hid_device_read_upgrade_response_retry_cb(FuDevice *device,
						    gpointer user_data,
						    GError **error)
{
	FuLegionHidRetryHelper *helper = (FuLegionHidRetryHelper *)user_data;
	GByteArray *res = helper->res;
	g_autoptr(FuStructLegionHidUpgradeRsp) st_rsp = NULL;

	g_byte_array_set_size(res, FU_LEGION_HID_DEVICE_FW_REPORT_LENGTH);
	if (!fu_udev_device_read(FU_UDEV_DEVICE(device),
				 res->data,
				 res->len,
				 NULL,
				 FU_LEGION_HID_DEVICE_IO_TIMEOUT,
				 FU_IO_CHANNEL_FLAG_NONE,
				 error))
		return FALSE;
	st_rsp = fu_struct_legion_hid_upgrade_rsp_parse(res->data, res->len, 0x0, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "device report upgrade command failed: ");
		return FALSE;
	}
	if (fu_struct_legion_hid_upgrade_rsp_get_main_id(st_rsp) != helper->main_id) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "response main ID was 0x%x, expected 0x%x",
			    fu_struct_legion_hid_upgrade_rsp_get_main_id(st_rsp),
			    helper->main_id);
		return FALSE;
	}
	if (fu_struct_legion_hid_upgrade_rsp_get_sub_id(st_rsp) != helper->sub_id) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "response sub ID was 0x%x, expected 0x%x",
			    fu_struct_legion_hid_upgrade_rsp_get_sub_id(st_rsp),
			    helper->sub_id);
		return FALSE;
	}
	if (fu_struct_legion_hid_upgrade_rsp_get_step(st_rsp) != helper->step) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "response step was 0x%x, expected 0x%x",
			    fu_struct_legion_hid_upgrade_rsp_get_step(st_rsp),
			    helper->step);
		return FALSE;
	}
	if (!fu_legion_hid_device_check_upgrade_device_id(
		fu_struct_legion_hid_upgrade_rsp_get_id(st_rsp),
		helper->dev_id)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "response dev ID was 0x%x, expected 0x%x",
			    fu_struct_legion_hid_upgrade_rsp_get_id(st_rsp),
			    helper->dev_id);
		return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_legion_hid_device_read_upgrade_response(FuLegionHidDevice *self,
					   FuLegionHidRetryHelper *helper,
					   GError **error)
{
	g_autoptr(GByteArray) res = g_byte_array_sized_new(FU_LEGION_HID_DEVICE_FW_REPORT_LENGTH);
	helper->res = res;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_legion_hid_device_read_upgrade_response_retry_cb,
				  120,
				  0,
				  helper,
				  error))
		return NULL;
	return g_steal_pointer(&res);
}

static gboolean
fu_legion_hid_device_read_upgrade_query_size_response_retry_cb(FuDevice *device,
							       gpointer user_data,
							       GError **error)
{
	FuLegionHidRetryHelper *helper = (FuLegionHidRetryHelper *)user_data;
	GByteArray *res = helper->res;
	g_autoptr(FuStructLegionHidUpgradeQuerySizeRsp) st_rsp = NULL;

	g_byte_array_set_size(res, FU_LEGION_HID_DEVICE_FW_REPORT_LENGTH);
	if (!fu_udev_device_read(FU_UDEV_DEVICE(device),
				 res->data,
				 res->len,
				 NULL,
				 FU_LEGION_HID_DEVICE_IO_TIMEOUT,
				 FU_IO_CHANNEL_FLAG_NONE,
				 error))
		return FALSE;
	st_rsp = fu_struct_legion_hid_upgrade_query_size_rsp_parse(res->data, res->len, 0x0, error);
	if (st_rsp == NULL) {
		g_prefix_error_literal(error, "device report upgrade command failed: ");
		return FALSE;
	}
	if (fu_struct_legion_hid_upgrade_query_size_rsp_get_main_id(st_rsp) != helper->main_id) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "response main ID was 0x%x, expected 0x%x",
			    fu_struct_legion_hid_upgrade_query_size_rsp_get_main_id(st_rsp),
			    helper->main_id);
		return FALSE;
	}
	if (fu_struct_legion_hid_upgrade_query_size_rsp_get_sub_id(st_rsp) != helper->sub_id) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "response sub ID was 0x%x, expected 0x%x",
			    fu_struct_legion_hid_upgrade_query_size_rsp_get_sub_id(st_rsp),
			    helper->sub_id);
		return FALSE;
	}
	if (fu_struct_legion_hid_upgrade_query_size_rsp_get_step(st_rsp) != helper->step) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "response step was 0x%x, expected 0x%x",
			    fu_struct_legion_hid_upgrade_query_size_rsp_get_step(st_rsp),
			    helper->step);
		return FALSE;
	}
	if (!fu_legion_hid_device_check_upgrade_device_id(
		fu_struct_legion_hid_upgrade_query_size_rsp_get_id(st_rsp),
		helper->dev_id)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_BUSY,
			    "response dev ID was 0x%x, expected 0x%x",
			    fu_struct_legion_hid_upgrade_query_size_rsp_get_id(st_rsp),
			    helper->dev_id);
		return FALSE;
	}
	if (fu_struct_legion_hid_upgrade_query_size_rsp_get_response(st_rsp) ==
	    FU_LEGION_HID_RESPONSE_STATUS_BUSY) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_BUSY,
				    "response report device is busy");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static GByteArray *
fu_legion_hid_device_read_upgrade_query_size_response(FuLegionHidDevice *self,
						      FuLegionHidRetryHelper *helper,
						      GError **error)
{
	g_autoptr(GByteArray) res = g_byte_array_sized_new(FU_LEGION_HID_DEVICE_FW_REPORT_LENGTH);
	helper->res = res;
	if (!fu_device_retry_full(FU_DEVICE(self),
				  fu_legion_hid_device_read_upgrade_query_size_response_retry_cb,
				  120,
				  0,
				  helper,
				  error))
		return NULL;
	return g_steal_pointer(&res);
}

static gboolean
fu_legion_hid_device_upgrade_start(FuLegionHidDevice *self,
				   FuLegionHidDeviceId id,
				   GInputStream *stream,
				   GError **error)
{
	FuLegionHidRetryHelper helper = {0};
	guint16 crc16 = 0;
	gsize streamsz = 0;
	g_autoptr(GByteArray) res = NULL;
	g_autoptr(FuStructLegionHidUpgradeCmd) st_cmd = fu_struct_legion_hid_upgrade_cmd_new();
	g_autoptr(FuStructLegionHidUpgradeStartParam) st_content =
	    fu_struct_legion_hid_upgrade_start_param_new();

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	if (!fu_input_stream_compute_crc16(stream, FU_CRC_KIND_B16_XMODEM, &crc16, error))
		return FALSE;
	fu_struct_legion_hid_upgrade_start_param_set_crc16(st_content, crc16);
	fu_struct_legion_hid_upgrade_start_param_set_size(st_content, streamsz);
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

	helper.main_id = fu_struct_legion_hid_upgrade_cmd_get_main_id(st_cmd);
	helper.sub_id = fu_struct_legion_hid_upgrade_cmd_get_sub_id(st_cmd);
	helper.dev_id = id;
	helper.step = FU_LEGION_HID_UPGRADE_STEP_START;
	res = fu_legion_hid_device_read_upgrade_response(self, &helper, error);
	if (res == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_legion_hid_device_upgrade_query_size(FuLegionHidDevice *self,
					FuLegionHidDeviceId id,
					guint16 *max_size,
					GError **error)
{
	FuLegionHidRetryHelper helper = {0};
	g_autoptr(FuStructLegionHidUpgradeQuerySizeRsp) st_rsp = NULL;
	g_autoptr(GByteArray) res = NULL;
	g_autoptr(FuStructLegionHidUpgradeCmd) st_cmd = fu_struct_legion_hid_upgrade_cmd_new();
	guint8 content[] = {
	    0x02,
	    FU_LEGION_HID_UPGRADE_STEP_QUERY_SIZE,
	    FU_LEGION_HID_CMD_CONSTANT_SN,
	};

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

	helper.main_id = fu_struct_legion_hid_upgrade_cmd_get_main_id(st_cmd);
	helper.sub_id = fu_struct_legion_hid_upgrade_cmd_get_sub_id(st_cmd);
	helper.dev_id = id;
	helper.step = FU_LEGION_HID_UPGRADE_STEP_QUERY_SIZE;
	res = fu_legion_hid_device_read_upgrade_query_size_response(self, &helper, error);
	if (res == NULL)
		return FALSE;
	st_rsp = fu_struct_legion_hid_upgrade_query_size_rsp_parse(res->data, res->len, 0x0, error);
	if (st_rsp == NULL)
		return FALSE;
	if (fu_struct_legion_hid_upgrade_query_size_rsp_get_response(st_rsp) ==
	    FU_LEGION_HID_RESPONSE_STATUS_FAIL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "device report query size command failed with %u",
			    fu_struct_legion_hid_upgrade_query_size_rsp_get_response(st_rsp));
		return FALSE;
	}
	if (max_size != NULL) {
		if (!fu_memread_uint16_safe(
			res->data,
			res->len,
			FU_STRUCT_LEGION_HID_UPGRADE_QUERY_SIZE_RSP_OFFSET_RESPONSE,
			max_size,
			G_BIG_ENDIAN,
			error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_legion_hid_device_upgrade_write_data_chunk(FuLegionHidDevice *self,
					      FuLegionHidDeviceId id,
					      guint16 max_size,
					      guint *send_size,
					      FuChunk *chk,
					      GError **error)
{
	guint ready_send_size = 0;
	g_autoptr(FuStructLegionHidUpgradeCmd) st_cmd = fu_struct_legion_hid_upgrade_cmd_new();
	g_autoptr(FuStructLegionHidUpgradePacket) st_packet =
	    fu_struct_legion_hid_upgrade_packet_new();

	if (!fu_struct_legion_hid_upgrade_packet_set_data(st_packet,
							  fu_chunk_get_data(chk),
							  fu_chunk_get_data_sz(chk),
							  error))
		return FALSE;
	fu_struct_legion_hid_upgrade_cmd_set_length(st_cmd, st_packet->buf->len + 5);
	fu_struct_legion_hid_upgrade_cmd_set_device_id(st_cmd, id);
	fu_struct_legion_hid_upgrade_cmd_set_param(st_cmd,
						   FU_LEGION_HID_CMD_CONSTANT_UPGRADE_SEND_DATA);
	if (!fu_struct_legion_hid_upgrade_cmd_set_data(st_cmd,
						       st_packet->buf->data,
						       st_packet->buf->len,
						       error))
		return FALSE;
	ready_send_size = *send_size + fu_chunk_get_data_sz(chk);
	if (!fu_udev_device_write(FU_UDEV_DEVICE(self),
				  st_cmd->buf->data,
				  st_cmd->buf->len,
				  FU_LEGION_HID_DEVICE_IO_TIMEOUT,
				  FU_IO_CHANNEL_FLAG_NONE,
				  error))
		return FALSE;
	*send_size = ready_send_size;
	if (ready_send_size % max_size == 0) {
		FuLegionHidRetryHelper helper = {0};
		guint32 recieved_size = 0;
		g_autoptr(GByteArray) res = NULL;

		helper.main_id = fu_struct_legion_hid_upgrade_cmd_get_main_id(st_cmd);
		helper.sub_id = fu_struct_legion_hid_upgrade_cmd_get_sub_id(st_cmd);
		helper.dev_id = id;
		helper.step = FU_LEGION_HID_UPGRADE_STEP_WRITE_DATA;
		res = fu_legion_hid_device_read_upgrade_response(self, &helper, error);
		if (res == NULL)
			return FALSE;
		if (!fu_memread_uint32_safe(res->data,
					    res->len,
					    FU_STRUCT_LEGION_HID_UPGRADE_RSP_SIZE,
					    &recieved_size,
					    G_BIG_ENDIAN,
					    error))
			return FALSE;
		if (recieved_size != *send_size) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "device report received size %u mismatch send size %u",
				    recieved_size,
				    *send_size);
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
fu_legion_hid_device_upgrade_write_data_chunks(FuLegionHidDevice *self,
					       FuLegionHidDeviceId id,
					       guint16 max_size,
					       FuChunkArray *chunks,
					       GError **error)
{
	guint send_size = 0;

	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;

		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;
		if (!fu_legion_hid_device_upgrade_write_data_chunk(self,
								   id,
								   max_size,
								   &send_size,
								   chk,
								   error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_legion_hid_device_upgrade_write_data(FuLegionHidDevice *self,
					FuLegionHidDeviceId id,
					guint16 max_size,
					GInputStream *stream,
					GError **error)
{
	g_autoptr(FuChunkArray) chunks = NULL;

	if (max_size == 0 || max_size % FU_LEGION_HID_DEVICE_FW_PACKET_LENGTH != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "device report max size %u is invalid",
			    max_size);
		return FALSE;
	}

	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						FU_LEGION_HID_DEVICE_FW_PACKET_LENGTH,
						error);
	if (chunks == NULL)
		return FALSE;
	return fu_legion_hid_device_upgrade_write_data_chunks(self, id, max_size, chunks, error);
}

static gboolean
fu_legion_hid_device_upgrade_verify(FuLegionHidDevice *self, FuLegionHidDeviceId id, GError **error)
{
	FuLegionHidRetryHelper helper = {0};
	g_autoptr(GByteArray) res = NULL;
	g_autoptr(FuStructLegionHidUpgradeCmd) st_cmd = fu_struct_legion_hid_upgrade_cmd_new();
	guint8 content[] = {
	    0x02,
	    FU_LEGION_HID_UPGRADE_STEP_VERIFY,
	    FU_LEGION_HID_CMD_CONSTANT_SN,
	};

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

	helper.main_id = fu_struct_legion_hid_upgrade_cmd_get_main_id(st_cmd);
	helper.sub_id = fu_struct_legion_hid_upgrade_cmd_get_sub_id(st_cmd);
	helper.dev_id = id;
	helper.step = FU_LEGION_HID_UPGRADE_STEP_VERIFY;
	res = fu_legion_hid_device_read_upgrade_response(self, &helper, error);
	if (res == NULL)
		return FALSE;

	/* success */
	return TRUE;
}

static gboolean
fu_legion_hid_device_get_version_internal(FuLegionHidDevice *self,
					  FuLegionHidDeviceId id,
					  guint32 *version,
					  GError **error)
{
	FuLegionHidRetryHelper helper = {0};
	guint8 content[] = {
	    FU_LEGION_HID_CMD_CONSTANT_SN,
	};
	g_autoptr(FuStructLegionHidNormalCmd) st_cmd = fu_struct_legion_hid_normal_cmd_new();
	g_autoptr(GByteArray) res = NULL;

	fu_struct_legion_hid_normal_cmd_set_length(st_cmd, sizeof(content) + 4);
	fu_struct_legion_hid_normal_cmd_set_main_id(st_cmd, 0x79);
	fu_struct_legion_hid_normal_cmd_set_sub_id(st_cmd, 0x01);
	fu_struct_legion_hid_normal_cmd_set_device_id(st_cmd, id);
	if (!fu_struct_legion_hid_normal_cmd_set_data(st_cmd, content, sizeof(content), error))
		return FALSE;
	if (!fu_udev_device_write(FU_UDEV_DEVICE(self),
				  st_cmd->buf->data,
				  st_cmd->buf->len,
				  FU_LEGION_HID_DEVICE_IO_TIMEOUT,
				  FU_IO_CHANNEL_FLAG_NONE,
				  error))
		return FALSE;

	helper.main_id = fu_struct_legion_hid_normal_cmd_get_main_id(st_cmd);
	helper.sub_id = fu_struct_legion_hid_normal_cmd_get_sub_id(st_cmd);
	helper.dev_id = id;
	res = fu_legion_hid_device_read_response(self, &helper, error);
	if (res == NULL)
		return FALSE;
	if (!fu_memread_uint32_safe(res->data, res->len, 13, version, G_BIG_ENDIAN, error))
		return FALSE;
	if (*version == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "version 0 is invalid");
		return FALSE;
	}

	/* success */
	return TRUE;
}

typedef struct {
	FuLegionHidDeviceId id;
	guint32 *version;
} FuLegionHidDeviceVersionHelper;

static gboolean
fu_legion_hid_device_get_version_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuLegionHidDevice *self = FU_LEGION_HID_DEVICE(device);
	FuLegionHidDeviceVersionHelper *helper = (FuLegionHidDeviceVersionHelper *)user_data;
	return fu_legion_hid_device_get_version_internal(self, helper->id, helper->version, error);
}

gboolean
fu_legion_hid_device_get_version(FuLegionHidDevice *self,
				 FuLegionHidDeviceId id,
				 guint32 *version,
				 GError **error)
{
	FuLegionHidDeviceVersionHelper helper = {
	    .id = id,
	    .version = version,
	};
	return fu_device_retry_full(FU_DEVICE(self),
				    fu_legion_hid_device_get_version_cb,
				    10,
				    2000,
				    &helper,
				    error);
}

static gboolean
fu_legion_hid_device_get_id(GInputStream *stream, FuLegionHidDeviceId *id, GError **error)
{
	guint offset = 0;
	gsize streamsz = 0;

	if (!fu_input_stream_size(stream, &streamsz, error))
		return FALSE;
	offset =
	    streamsz - FU_LEGION_HID_DEVICE_FW_SIGNED_LENGTH - FU_LEGION_HID_DEVICE_FW_ID_LENGTH;
	for (guint i = 0; i < FU_LEGION_HID_DEVICE_FW_ID_LENGTH; i++) {
		guint8 id_tmp = 0;
		if (!fu_input_stream_read_u8(stream, offset + i, &id_tmp, error))
			return FALSE;
		if (id_tmp == FU_LEGION_HID_DEVICE_ID_DONGLE ||
		    id_tmp == FU_LEGION_HID_DEVICE_ID_GAMEPAD_L3 ||
		    id_tmp == FU_LEGION_HID_DEVICE_ID_GAMEPAD_R3) {
			if (id != NULL)
				*id = id_tmp;
			return TRUE;
		}
	}
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INTERNAL,
			    "firmware device id is invalid");
	return FALSE;
}

gboolean
fu_legion_hid_device_execute_upgrade(FuLegionHidDevice *self, FuFirmware *firmware, GError **error)
{
	FuLegionHidDeviceId id = 0;
	guint16 max_size = 0;
	g_autoptr(GInputStream) stream = NULL;

	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;
	if (!fu_legion_hid_device_get_id(stream, &id, error))
		return FALSE;
	if (!fu_legion_hid_device_upgrade_start(self, id, stream, error))
		return FALSE;
	if (!fu_legion_hid_device_upgrade_query_size(self, id, &max_size, error))
		return FALSE;
	if (!fu_legion_hid_device_upgrade_write_data(self, id, max_size, stream, error))
		return FALSE;
	if (!fu_legion_hid_device_upgrade_verify(self, id, error))
		return FALSE;

	/* success */
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
	g_autoptr(FuFirmware) img = NULL;

	img = fu_firmware_get_image_by_id(firmware, FU_LEGION_HID_FIRMWARE_ID_MCU, error);
	if (img == NULL)
		return FALSE;
	if (!fu_legion_hid_device_execute_upgrade(self, img, error)) {
		g_prefix_error_literal(error, "execute upgrade mcu failed: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gchar *
fu_legion_hid_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return g_strdup_printf("%X", (guint)version_raw);
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
fu_legion_hid_device_setup(FuDevice *device, GError **error)
{
	FuLegionHidDevice *self = FU_LEGION_HID_DEVICE(device);
	guint32 version = 0;
	g_autoptr(FuHidDescriptor) descriptor = NULL;
	g_autoptr(FuHidReport) report = NULL;
	g_autoptr(FuLegionHidChild) child_left = NULL;
	g_autoptr(FuLegionHidChild) child_right = NULL;

	/* device needs to be open, hence not in ->probe() */
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

	/* MCU */
	if (!fu_legion_hid_device_get_version(self, FU_LEGION_HID_DEVICE_ID_RX, &version, error))
		return FALSE;
	fu_device_set_version_raw(FU_DEVICE(self), version);

	/* left */
	child_left = fu_legion_hid_child_new(device, FU_LEGION_HID_DEVICE_ID_GAMEPAD_L);
	fu_device_set_logical_id(FU_DEVICE(child_left), FU_LEGION_HID_FIRMWARE_ID_LEFT);
	fu_device_set_name(FU_DEVICE(child_left), "Left");
	fu_device_add_child(device, FU_DEVICE(child_left));

	/* right */
	child_right = fu_legion_hid_child_new(device, FU_LEGION_HID_DEVICE_ID_GAMEPAD_R);
	fu_device_set_logical_id(FU_DEVICE(child_right), FU_LEGION_HID_FIRMWARE_ID_RIGHT);
	fu_device_set_name(FU_DEVICE(child_right), "Right");
	fu_device_add_child(device, FU_DEVICE(child_right));

	/* success */
	return TRUE;
}

static void
fu_legion_hid_device_class_init(FuLegionHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_legion_hid_device_setup;
	device_class->write_firmware = fu_legion_hid_device_write_firmware;
	device_class->set_progress = fu_legion_hid_device_set_progress;
	device_class->convert_version = fu_legion_hid_device_convert_version;
}

static void
fu_legion_hid_device_init(FuLegionHidDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.legion-hid");
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_LEGION_HID_FIRMWARE);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_READ);
	fu_udev_device_add_open_flag(FU_UDEV_DEVICE(self), FU_IO_CHANNEL_OPEN_FLAG_WRITE);
}

/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-hailuck-common.h"
#include "fu-hailuck-struct.h"
#include "fu-hailuck-tp-device.h"

struct _FuHailuckTpDevice {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuHailuckTpDevice, fu_hailuck_tp_device, FU_TYPE_DEVICE)

static gboolean
fu_hailuck_tp_device_probe(FuDevice *device, GError **error)
{
	/* add extra touchpad-specific GUID */
	fu_device_add_instance_str(device, "MODE", "TP");
	return fu_device_build_instance_id(device, error, "USB", "VID", "PID", "MODE", NULL);
}

typedef struct {
	guint8 type;
	guint8 success; /* if 0xff, then cmd-0x10 */
} FuHailuckTpDeviceReq;

static gboolean
fu_hailuck_tp_device_cmd_cb(FuDevice *device, gpointer user_data, GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	FuHailuckTpDeviceReq *req = (FuHailuckTpDeviceReq *)user_data;
	guint8 buf[6] = {
	    FU_HAILUCK_REPORT_ID_SHORT,
	    FU_HAILUCK_CMD_GET_STATUS,
	    req->type,
	};
	guint8 success_tmp = req->success;
	if (!fu_hid_device_set_report(FU_HID_DEVICE(parent),
				      buf[0],
				      buf,
				      sizeof(buf),
				      1000,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error))
		return FALSE;
	if (!fu_hid_device_get_report(FU_HID_DEVICE(parent),
				      buf[0],
				      buf,
				      sizeof(buf),
				      2000,
				      FU_HID_DEVICE_FLAG_IS_FEATURE |
					  FU_HID_DEVICE_FLAG_ALLOW_TRUNC,
				      error))
		return FALSE;
	if (success_tmp == 0xff)
		success_tmp = req->type - 0x10;
	if (buf[0] != FU_HAILUCK_REPORT_ID_SHORT || buf[1] != success_tmp) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "report mismatch for type=0x%02x[%s]: "
			    "expected=0x%02x, received=0x%02x",
			    req->type,
			    fu_hailuck_cmd_to_string(req->type),
			    success_tmp,
			    buf[1]);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_hailuck_tp_device_write_firmware(FuDevice *device,
				    FuFirmware *firmware,
				    FuProgress *progress,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuDevice *parent = fu_device_get_parent(device);
	const guint block_size = 1024;
	g_autoptr(GInputStream) stream = NULL;
	g_autoptr(FuChunkArray) chunks = NULL;
	FuHailuckTpDeviceReq req = {
	    .type = 0xff,
	    .success = 0xff,
	};

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 10, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 85, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "end-program");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_VERIFY, 3, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 1, "pass");

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* erase */
	req.type = FU_HAILUCK_CMD_I2C_ERASE;
	if (!fu_device_retry(device, fu_hailuck_tp_device_cmd_cb, 100, &req, error)) {
		g_prefix_error(error, "failed to erase: ");
		return FALSE;
	}
	fu_device_sleep(device, 10);
	fu_progress_step_done(progress);

	/* write */
	chunks = fu_chunk_array_new_from_stream(stream,
						FU_CHUNK_ADDR_OFFSET_NONE,
						FU_CHUNK_PAGESZ_NONE,
						block_size,
						error);
	if (chunks == NULL)
		return FALSE;
	for (guint i = 0; i < fu_chunk_array_length(chunks); i++) {
		g_autoptr(FuChunk) chk = NULL;
		g_autoptr(GByteArray) buf = g_byte_array_new();

		/* prepare chunk */
		chk = fu_chunk_array_index(chunks, i, error);
		if (chk == NULL)
			return FALSE;

		/* write block */
		fu_byte_array_append_uint8(buf, FU_HAILUCK_REPORT_ID_LONG);
		fu_byte_array_append_uint8(buf, FU_HAILUCK_CMD_WRITE_TP);
		fu_byte_array_append_uint16(buf, 0xCCCC, G_LITTLE_ENDIAN);
		fu_byte_array_append_uint16(buf, fu_chunk_get_address(chk), G_LITTLE_ENDIAN);
		fu_byte_array_append_uint16(buf, 0xCCCC, G_LITTLE_ENDIAN);
		g_byte_array_append(buf, fu_chunk_get_data(chk), fu_chunk_get_data_sz(chk));
		fu_byte_array_append_uint8(buf, 0xEE);
		fu_byte_array_append_uint8(buf, 0xD2);
		fu_byte_array_append_uint16(buf, 0xCCCC, G_LITTLE_ENDIAN);
		fu_byte_array_append_uint16(buf, 0xCCCC, G_LITTLE_ENDIAN);
		fu_byte_array_append_uint16(buf, 0xCCCC, G_LITTLE_ENDIAN);
		if (buf->len != block_size + 16) {
			g_set_error(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "packet mismatch: len=0x%04x, expected=0x%04x",
				    buf->len,
				    block_size + 16);
			return FALSE;
		}
		if (!fu_hid_device_set_report(FU_HID_DEVICE(parent),
					      buf->data[0],
					      buf->data,
					      buf->len,
					      1000,
					      FU_HID_DEVICE_FLAG_IS_FEATURE,
					      error)) {
			g_prefix_error(error, "failed to write block 0x%x: ", i);
			return FALSE;
		}
		fu_device_sleep(device, 150);

		/* verify block */
		req.type = FU_HAILUCK_CMD_I2C_VERIFY_BLOCK;
		if (!fu_device_retry(device, fu_hailuck_tp_device_cmd_cb, 100, &req, error)) {
			g_prefix_error(error, "failed to verify block 0x%x: ", i);
			return FALSE;
		}

		/* update progress */
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						i + 1,
						fu_chunk_array_length(chunks));
	}
	fu_device_sleep(device, 50);
	fu_progress_step_done(progress);

	/* end-program */
	req.type = FU_HAILUCK_CMD_I2C_END_PROGRAM;
	if (!fu_device_retry(device, fu_hailuck_tp_device_cmd_cb, 100, &req, error)) {
		g_prefix_error(error, "failed to end program: ");
		return FALSE;
	}
	fu_device_sleep(device, 50);
	fu_progress_step_done(progress);

	/* verify checksum */
	req.type = FU_HAILUCK_CMD_I2C_VERIFY_CHECKSUM;
	if (!fu_device_retry(device, fu_hailuck_tp_device_cmd_cb, 100, &req, error)) {
		g_prefix_error(error, "failed to verify: ");
		return FALSE;
	}
	fu_device_sleep(device, 50);
	fu_progress_step_done(progress);

	/* signal that programming has completed */
	req.type = FU_HAILUCK_CMD_I2C_PROGRAMPASS;
	req.success = 0x0;
	if (!fu_device_retry(device, fu_hailuck_tp_device_cmd_cb, 100, &req, error)) {
		g_prefix_error(error, "failed to program: ");
		return FALSE;
	}
	fu_progress_step_done(progress);

	/* success! */
	return TRUE;
}

static void
fu_hailuck_tp_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 94, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 2, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_hailuck_tp_device_init(FuHailuckTpDevice *self)
{
	fu_device_retry_set_delay(FU_DEVICE(self), 50); /* ms */
	fu_device_set_firmware_size(FU_DEVICE(self), 0x6018);
	fu_device_add_protocol(FU_DEVICE(self), "com.hailuck.tp");
	fu_device_set_logical_id(FU_DEVICE(self), "TP");
	fu_device_set_name(FU_DEVICE(self), "Touchpad");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PARENT_FOR_OPEN);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_TOUCHPAD);
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
}

static void
fu_hailuck_tp_device_class_init(FuHailuckTpDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_hailuck_tp_device_write_firmware;
	device_class->probe = fu_hailuck_tp_device_probe;
	device_class->set_progress = fu_hailuck_tp_device_set_progress;
}

FuHailuckTpDevice *
fu_hailuck_tp_device_new(FuDevice *parent)
{
	FuHailuckTpDevice *self;
	self = g_object_new(FU_TYPE_HAILUCK_TP_DEVICE, "parent", parent, NULL);
	return FU_HAILUCK_TP_DEVICE(self);
}

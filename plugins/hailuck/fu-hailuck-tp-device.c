/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-hailuck-common.h"
#include "fu-hailuck-tp-device.h"

struct _FuHailuckTpDevice {
	FuDevice		 parent_instance;
};

G_DEFINE_TYPE (FuHailuckTpDevice, fu_hailuck_tp_device, FU_TYPE_DEVICE)

static gboolean
fu_hailuck_tp_device_probe (FuDevice *device, GError **error)
{
	FuDevice *parent = fu_device_get_parent (device);
	g_autofree gchar *devid1 = NULL;
	g_autofree gchar *devid2 = NULL;

	devid1 = g_strdup_printf ("USB\\VID_%04X&PID_%04X",
				  fu_usb_device_get_vid (FU_USB_DEVICE (parent)),
				  fu_usb_device_get_pid (FU_USB_DEVICE (parent)));
	fu_device_add_instance_id (device, devid1);
	devid2 = g_strdup_printf ("USB\\VID_%04X&PID_%04X&MODE_TP",
				  fu_usb_device_get_vid (FU_USB_DEVICE (parent)),
				  fu_usb_device_get_pid (FU_USB_DEVICE (parent)));
	fu_device_add_instance_id (device, devid2);

	return TRUE;
}

typedef struct {
	guint8		type;
	guint8		success; /* if 0xff, then cmd-0x10 */
} FuHailuckTpDeviceReq;

static gboolean
fu_hailuck_tp_device_cmd_cb (FuDevice *device, gpointer user_data, GError **error)
{
	FuDevice *parent = fu_device_get_parent (device);
	FuHailuckTpDeviceReq *req = (FuHailuckTpDeviceReq *) user_data;
	guint8 buf[6] = {
		FU_HAILUCK_REPORT_ID_SHORT,
		FU_HAILUCK_CMD_GET_STATUS,
		req->type,
	};
	guint8 success_tmp = req->success;
	if (!fu_hid_device_set_report (FU_HID_DEVICE (parent), buf[0],
				       buf, sizeof(buf), 1000,
				       FU_HID_DEVICE_FLAG_IS_FEATURE, error))
		return FALSE;
	if (!fu_hid_device_get_report (FU_HID_DEVICE (parent), buf[0],
				       buf, sizeof(buf), 2000,
				       FU_HID_DEVICE_FLAG_IS_FEATURE |
				       FU_HID_DEVICE_FLAG_ALLOW_TRUNC,
				       error))
		return FALSE;
	if (success_tmp == 0xff)
		success_tmp = req->type - 0x10;
	if (buf[0] != FU_HAILUCK_REPORT_ID_SHORT || buf[1] != success_tmp) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_FAILED,
			     "report mismatch for type=0x%02x[%s]: "
			     "expected=0x%02x, received=0x%02x",
			     req->type,
			     fu_hailuck_cmd_to_string (req->type),
			     success_tmp, buf[1]);
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
	FuDevice *parent = fu_device_get_parent (device);
	const guint block_size = 1024;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;
	FuHailuckTpDeviceReq req = {
		.type = 0xff,
		.success = 0xff,
	};

	/* get default image */
	fw = fu_firmware_get_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;

	/* erase */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
	req.type = FU_HAILUCK_CMD_I2C_ERASE;
	if (!fu_device_retry (device, fu_hailuck_tp_device_cmd_cb, 100, &req, error)) {
		g_prefix_error (error, "failed to erase: ");
		return FALSE;
	}
	g_usleep (10000);

	/* write */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	chunks = fu_chunk_array_new_from_bytes (fw, 0x0, 0x0, block_size);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		g_autoptr(GByteArray) buf = g_byte_array_new ();

		/* write block */
		fu_byte_array_append_uint8 (buf, FU_HAILUCK_REPORT_ID_LONG);
		fu_byte_array_append_uint8 (buf, FU_HAILUCK_CMD_WRITE_TP);
		fu_byte_array_append_uint16 (buf, 0xCCCC, G_LITTLE_ENDIAN);
		fu_byte_array_append_uint16 (buf, fu_chunk_get_address (chk), G_LITTLE_ENDIAN);
		fu_byte_array_append_uint16 (buf, 0xCCCC, G_LITTLE_ENDIAN);
		g_byte_array_append (buf, fu_chunk_get_data (chk), fu_chunk_get_data_sz (chk));
		fu_byte_array_append_uint8 (buf, 0xEE);
		fu_byte_array_append_uint8 (buf, 0xD2);
		fu_byte_array_append_uint16 (buf, 0xCCCC, G_LITTLE_ENDIAN);
		fu_byte_array_append_uint16 (buf, 0xCCCC, G_LITTLE_ENDIAN);
		fu_byte_array_append_uint16 (buf, 0xCCCC, G_LITTLE_ENDIAN);
		if (buf->len != block_size + 16) {
			g_set_error (error,
				     G_IO_ERROR,
				     G_IO_ERROR_FAILED,
				     "packet mismatch: len=0x%04x, expected=0x%04x",
				     buf->len, block_size + 16);
			return FALSE;
		}
		if (!fu_hid_device_set_report (FU_HID_DEVICE (parent), buf->data[0],
					       buf->data, buf->len, 1000,
					       FU_HID_DEVICE_FLAG_IS_FEATURE, error)) {
			g_prefix_error (error, "failed to write block 0x%x: ", i);
			return FALSE;
		}
		g_usleep (150 * 1000);

		/* verify block */
		req.type = FU_HAILUCK_CMD_I2C_VERIFY_BLOCK;
		if (!fu_device_retry (device, fu_hailuck_tp_device_cmd_cb, 100, &req, error)) {
			g_prefix_error (error, "failed to verify block 0x%x: ", i);
			return FALSE;
		}

		/* update progress */
		fu_progress_set_percentage_full(progress, i, chunks->len - 1);
	}
	g_usleep (50 * 1000);

	/* end-program */
	req.type = FU_HAILUCK_CMD_I2C_END_PROGRAM;
	if (!fu_device_retry (device, fu_hailuck_tp_device_cmd_cb, 100, &req, error)) {
		g_prefix_error (error, "failed to end program: ");
		return FALSE;
	}
	g_usleep (50 * 1000);

	/* verify checksum */
	req.type = FU_HAILUCK_CMD_I2C_VERIFY_CHECKSUM;
	if (!fu_device_retry (device, fu_hailuck_tp_device_cmd_cb, 100, &req, error)) {
		g_prefix_error (error, "failed to verify: ");
		return FALSE;
	}
	g_usleep (50 * 1000);

	/* signal that programming has completed */
	req.type = FU_HAILUCK_CMD_I2C_PROGRAMPASS;
	req.success = 0x0;
	if (!fu_device_retry (device, fu_hailuck_tp_device_cmd_cb, 100, &req, error)) {
		g_prefix_error (error, "failed to program: ");
		return FALSE;
	}

	/* success! */
	return TRUE;
}

static void
fu_hailuck_tp_device_init (FuHailuckTpDevice *self)
{
	fu_device_retry_set_delay (FU_DEVICE (self), 50); /* ms */
	fu_device_set_firmware_size (FU_DEVICE (self), 0x6018);
	fu_device_add_protocol (FU_DEVICE (self), "com.hailuck.tp");
	fu_device_set_logical_id (FU_DEVICE (self), "TP");
	fu_device_set_name (FU_DEVICE (self), "Touchpad");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_internal_flag (FU_DEVICE (self), FU_DEVICE_INTERNAL_FLAG_USE_PARENT_FOR_OPEN);
	fu_device_add_icon (FU_DEVICE (self), "input-touchpad");
	fu_device_set_remove_delay (FU_DEVICE (self),
				    FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
}

static void
fu_hailuck_tp_device_class_init (FuHailuckTpDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->write_firmware = fu_hailuck_tp_device_write_firmware;
	klass_device->probe = fu_hailuck_tp_device_probe;
}

FuHailuckTpDevice *
fu_hailuck_tp_device_new (FuDevice *device)
{
	FuHailuckTpDevice *self;
	self = g_object_new (FU_TYPE_HAILUCK_TP_DEVICE,
			     "parent", device,
			     NULL);
	return FU_HAILUCK_TP_DEVICE (self);
}

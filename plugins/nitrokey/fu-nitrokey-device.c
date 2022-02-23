/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-nitrokey-common.h"
#include "fu-nitrokey-device.h"

G_DEFINE_TYPE(FuNitrokeyDevice, fu_nitrokey_device, FU_TYPE_HID_DEVICE)

typedef struct {
	guint8 command;
	const guint8 *buf_in;
	gsize buf_in_sz;
	guint8 *buf_out;
	gsize buf_out_sz;
} NitrokeyRequest;

static gboolean
nitrokey_execute_cmd_cb(FuDevice *device, gpointer user_data, GError **error)
{
	NitrokeyRequest *req = (NitrokeyRequest *)user_data;
	NitrokeyHidResponse res;
	guint32 crc_tmp;
	guint8 buf[64];

	/* create the request */
	memset(buf, 0x00, sizeof(buf));
	buf[0] = req->command;
	if (req->buf_in != NULL)
		memcpy(&buf[1], req->buf_in, req->buf_in_sz);
	crc_tmp = fu_nitrokey_perform_crc32(buf, sizeof(buf) - 4);
	fu_common_write_uint32(&buf[NITROKEY_REQUEST_DATA_LENGTH + 1], crc_tmp, G_LITTLE_ENDIAN);

	/* send request */
	if (!fu_hid_device_set_report(FU_HID_DEVICE(device),
				      0x0002,
				      buf,
				      sizeof(buf),
				      NITROKEY_TRANSACTION_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error))
		return FALSE;

	/* get response */
	memset(buf, 0x00, sizeof(buf));
	if (!fu_hid_device_get_report(FU_HID_DEVICE(device),
				      0x0002,
				      buf,
				      sizeof(buf),
				      NITROKEY_TRANSACTION_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error))
		return FALSE;

	/* verify this is the answer to the question we asked */
	memcpy(&res, buf, sizeof(buf));
	if (GUINT32_FROM_LE(res.last_command_crc) != crc_tmp) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "got response CRC %x, expected %x",
			    GUINT32_FROM_LE(res.last_command_crc),
			    crc_tmp);
		return FALSE;
	}

	/* verify the response checksum */
	crc_tmp = fu_nitrokey_perform_crc32(buf, sizeof(res) - 4);
	if (GUINT32_FROM_LE(res.crc) != crc_tmp) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "got packet CRC %x, expected %x",
			    GUINT32_FROM_LE(res.crc),
			    crc_tmp);
		return FALSE;
	}

	/* copy out the payload */
	if (req->buf_out != NULL)
		memcpy(req->buf_out, &res.payload, req->buf_out_sz);

	/* success */
	return TRUE;
}

static gboolean
nitrokey_execute_cmd_full(FuDevice *device,
			  guint8 command,
			  const guint8 *buf_in,
			  gsize buf_in_sz,
			  guint8 *buf_out,
			  gsize buf_out_sz,
			  GError **error)
{
	NitrokeyRequest req = {
	    .command = command,
	    .buf_in = buf_in,
	    .buf_in_sz = buf_in_sz,
	    .buf_out = buf_out,
	    .buf_out_sz = buf_out_sz,
	};
	g_return_val_if_fail(buf_in_sz <= NITROKEY_REQUEST_DATA_LENGTH, FALSE);
	g_return_val_if_fail(buf_out_sz <= NITROKEY_REPLY_DATA_LENGTH, FALSE);
	return fu_device_retry(device, nitrokey_execute_cmd_cb, NITROKEY_NR_RETRIES, &req, error);
}

static gboolean
fu_nitrokey_device_setup(FuDevice *device, GError **error)
{
	NitrokeyGetDeviceStatusPayload payload;
	guint8 buf_reply[NITROKEY_REPLY_DATA_LENGTH];
	g_autofree gchar *version = NULL;

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_nitrokey_device_parent_class)->setup(device, error))
		return FALSE;

	/* get firmware version */
	if (!nitrokey_execute_cmd_full(device,
				       NITROKEY_CMD_GET_DEVICE_STATUS,
				       NULL,
				       0,
				       buf_reply,
				       sizeof(buf_reply),
				       error)) {
		g_prefix_error(error, "failed to do get firmware version: ");
		return FALSE;
	}
	if (g_getenv("FWUPD_NITROKEY_VERBOSE") != NULL)
		fu_common_dump_raw(G_LOG_DOMAIN, "payload", buf_reply, sizeof(buf_reply));
	memcpy(&payload, buf_reply, sizeof(payload));
	version = g_strdup_printf("%u.%u", payload.VersionMajor, payload.VersionMinor);
	fu_device_set_version(FU_DEVICE(device), version);

	/* success */
	return TRUE;
}

static void
fu_nitrokey_device_init(FuNitrokeyDevice *self)
{
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
	fu_device_add_protocol(FU_DEVICE(self), "org.usb.dfu");
	fu_device_retry_set_delay(FU_DEVICE(self), 100);
}

static void
fu_nitrokey_device_class_init(FuNitrokeyDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->setup = fu_nitrokey_device_setup;
}

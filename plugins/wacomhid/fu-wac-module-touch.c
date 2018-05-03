/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-wac-device.h"
#include "fu-wac-module-touch.h"

#include "dfu-chunked.h"

struct _FuWacModuleTouch
{
	FuWacModule		 parent_instance;
};

G_DEFINE_TYPE (FuWacModuleTouch, fu_wac_module_touch, FU_TYPE_WAC_MODULE)

static gboolean
fu_wac_module_touch_write_firmware (FuDevice *device, GBytes *blob, GError **error)
{
	FuWacDevice *parent = FU_WAC_DEVICE (fu_device_get_parent (device));
	FuWacModule *self = FU_WAC_MODULE (device);
	const guint8 *data;
	gsize blocks_total = 0;
	gsize len = 0;
	g_autoptr(GPtrArray) chunks = NULL;

	/* build each data packet */
	data = g_bytes_get_data (blob, &len);
	if (len % 128 != 0) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "firmware has to be padded to 128b");
		return FALSE;
	}
	chunks = dfu_chunked_new (data, (guint32) len,
				  0x0, /* addr_start */
				  0x0, /* page_sz */
				  128); /* packet_sz */
	blocks_total = chunks->len + 2;

	/* start, which will erase the module */
	if (!fu_wac_module_set_feature (self, FU_WAC_MODULE_COMMAND_START, NULL, error))
		return FALSE;

	/* update progress */
	fu_device_set_progress_full (device, 1, blocks_total);

	/* data */
	for (guint i = 0; i < chunks->len; i++) {
		DfuChunkedPacket *pkt = g_ptr_array_index (chunks, i);
		guint8 buf[128+7];
		g_autoptr(GBytes) blob_chunk = NULL;

		/* build G11T data packet */
		memset (buf, 0xff, sizeof(buf));
		buf[0] = 0x01; /* writing */
		fu_common_write_uint32 (&buf[1], pkt->address, G_LITTLE_ENDIAN);
		buf[5] = pkt->idx;
		memcpy (&buf[6], pkt->data, pkt->data_sz);
		blob_chunk = g_bytes_new (buf, sizeof(buf));
		if (!fu_wac_module_set_feature (self, FU_WAC_MODULE_COMMAND_DATA,
						blob_chunk, error))
			return FALSE;

		/* update progress */
		fu_device_set_progress_full (device, i + 1, blocks_total);
	}

	/* end */
	if (!fu_wac_module_set_feature (self, FU_WAC_MODULE_COMMAND_END, NULL, error))
		return FALSE;

	/* update progress */
	fu_device_set_progress_full (device, blocks_total, blocks_total);

	/* reboot */
	fu_device_set_status (FU_DEVICE (self), FWUPD_STATUS_DEVICE_RESTART);
	return fu_wac_device_update_reset (parent, error);
}

static void
fu_wac_module_touch_init (FuWacModuleTouch *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_name (FU_DEVICE (self), "Touch Module");
}

static void
fu_wac_module_touch_class_init (FuWacModuleTouchClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->write_firmware = fu_wac_module_touch_write_firmware;
}

FuWacModule *
fu_wac_module_touch_new (GUsbDevice *usb_device)
{
	FuWacModule *module = NULL;
	module = g_object_new (FU_TYPE_WAC_MODULE_TOUCH,
			       "usb-device", usb_device,
			       "fw-type", FU_WAC_MODULE_FW_TYPE_TOUCH,
			       NULL);
	return module;
}

/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-wac-device.h"
#include "fu-wac-module-touch.h"

#include "fu-chunk.h"
#include "dfu-firmware.h"

struct _FuWacModuleTouch
{
	FuWacModule		 parent_instance;
};

G_DEFINE_TYPE (FuWacModuleTouch, fu_wac_module_touch, FU_TYPE_WAC_MODULE)

static gboolean
fu_wac_module_touch_write_firmware (FuDevice *device, GBytes *blob, GError **error)
{
	DfuElement *element;
	DfuImage *image;
	FuWacModule *self = FU_WAC_MODULE (device);
	gsize blocks_total = 0;
	g_autoptr(GPtrArray) chunks = NULL;
	g_autoptr(DfuFirmware) firmware = dfu_firmware_new ();

	/* load .hex file */
	if (!dfu_firmware_parse_data (firmware, blob, DFU_FIRMWARE_PARSE_FLAG_NONE, error))
		return FALSE;

	/* check type */
	if (dfu_firmware_get_format (firmware) != DFU_FIRMWARE_FORMAT_INTEL_HEX) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_INTERNAL,
			     "expected firmware format is 'ihex', got '%s'",
			     dfu_firmware_format_to_string (dfu_firmware_get_format (firmware)));
		return FALSE;
	}

	/* use the correct image from the firmware */
	image = dfu_firmware_get_image (firmware, 0);
	if (image == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no firmware image");
		return FALSE;
	}
	element = dfu_image_get_element_default (image);
	if (element == NULL) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_INTERNAL,
				     "no firmware element");
		return FALSE;
	}
	g_debug ("using element at addr 0x%0x",
		 (guint) dfu_element_get_address (element));
	blob = dfu_element_get_contents (element);

	/* build each data packet */
	chunks = fu_chunk_array_new_from_bytes (blob,
						0x0, /* addr_start */
						0x0, /* page_sz */
						128); /* packet_sz */
	blocks_total = chunks->len + 2;

	/* start, which will erase the module */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
	if (!fu_wac_module_set_feature (self, FU_WAC_MODULE_COMMAND_START, NULL, error))
		return FALSE;

	/* update progress */
	fu_device_set_progress_full (device, 1, blocks_total);

	/* data */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);
		guint8 buf[128+7] = { 0xff };
		g_autoptr(GBytes) blob_chunk = NULL;

		/* build G11T data packet */
		memset (buf, 0xff, sizeof(buf));
		buf[0] = 0x01; /* writing */
		fu_common_write_uint32 (&buf[1], chk->address, G_BIG_ENDIAN);
		buf[5] = chk->idx;
		memcpy (&buf[6], chk->data, chk->data_sz);
		blob_chunk = g_bytes_new (buf, sizeof(buf));
		if (!fu_wac_module_set_feature (self, FU_WAC_MODULE_COMMAND_DATA,
						blob_chunk, error)) {
			g_prefix_error (error, "failed to write block %u: ", chk->idx);
			return FALSE;
		}

		/* update progress */
		fu_device_set_progress_full (device, i + 1, blocks_total);
	}

	/* end */
	if (!fu_wac_module_set_feature (self, FU_WAC_MODULE_COMMAND_END, NULL, error))
		return FALSE;

	/* update progress */
	fu_device_set_progress_full (device, blocks_total, blocks_total);
	return TRUE;
}

static void
fu_wac_module_touch_init (FuWacModuleTouch *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_install_duration (FU_DEVICE (self), 30);
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

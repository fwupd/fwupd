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
#include "fu-ihex-firmware.h"

struct _FuWacModuleTouch
{
	FuWacModule		 parent_instance;
};

G_DEFINE_TYPE (FuWacModuleTouch, fu_wac_module_touch, FU_TYPE_WAC_MODULE)

static FuFirmware *
fu_wac_module_touch_prepare_firmware (FuDevice *device,
				      GBytes *fw,
				      FwupdInstallFlags flags,
				      GError **error)
{
	g_autoptr(FuFirmware) firmware = fu_ihex_firmware_new ();
	if (!fu_firmware_parse (firmware, fw, flags, error))
		return NULL;
	return g_steal_pointer (&firmware);
}

static gboolean
fu_wac_module_touch_write_firmware (FuDevice *device,
				    FuFirmware *firmware,
				    FwupdInstallFlags flags,
				    GError **error)
{
	FuWacModule *self = FU_WAC_MODULE (device);
	gsize blocks_total = 0;
	g_autoptr(FuFirmwareImage) img = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* use the correct image from the firmware */
	img = fu_firmware_get_image_default (firmware, error);
	if (img == NULL)
		return FALSE;
	g_debug ("using element at addr 0x%0x",
		 (guint) fu_firmware_image_get_addr (img));
	fw = fu_firmware_image_write (img, error);
	if (fw == NULL)
		return FALSE;

	/* build each data packet */
	chunks = fu_chunk_array_new_from_bytes (fw,
						fu_firmware_image_get_addr (img),
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
		buf[1] = chk->idx + 1;
		fu_common_write_uint32 (&buf[2], chk->address, G_LITTLE_ENDIAN);
		buf[6] = 0x10; /* no idea! */
		memcpy (&buf[7], chk->data, chk->data_sz);
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
	klass_device->prepare_firmware = fu_wac_module_touch_prepare_firmware;
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

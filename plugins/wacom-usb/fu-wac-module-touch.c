/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include <string.h>

#include "fu-wac-device.h"
#include "fu-wac-module-touch.h"

struct _FuWacModuleTouch {
	FuWacModule parent_instance;
};

G_DEFINE_TYPE(FuWacModuleTouch, fu_wac_module_touch, FU_TYPE_WAC_MODULE)

static gboolean
fu_wac_module_touch_write_firmware(FuDevice *device,
				   FuFirmware *firmware,
				   FuProgress *progress,
				   FwupdInstallFlags flags,
				   GError **error)
{
	FuWacModule *self = FU_WAC_MODULE(device);
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_ERASE, 10, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10, NULL);

	g_debug("using element at addr 0x%0x", (guint)fu_firmware_get_addr(firmware));

	/* build each data packet */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	chunks = fu_chunk_array_new_from_bytes(fw,
					       fu_firmware_get_addr(firmware),
					       0x0,  /* page_sz */
					       128); /* packet_sz */

	/* start, which will erase the module */
	if (!fu_wac_module_set_feature(self,
				       FU_WAC_MODULE_COMMAND_START,
				       NULL,
				       fu_progress_get_child(progress),
				       FU_WAC_MODULE_ERASE_TIMEOUT,
				       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* data */
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index(chunks, i);
		guint8 buf[128 + 7] = {0xff};
		g_autoptr(GBytes) blob_chunk = NULL;

		/* build G11T data packet */
		memset(buf, 0xff, sizeof(buf));
		buf[0] = 0x01; /* writing */
		buf[1] = fu_chunk_get_idx(chk) + 1;
		fu_memwrite_uint32(&buf[2], fu_chunk_get_address(chk), G_LITTLE_ENDIAN);
		buf[6] = 0x10; /* no idea! */
		if (!fu_memcpy_safe(buf,
				    sizeof(buf),
				    0x07, /* dst */
				    fu_chunk_get_data(chk),
				    fu_chunk_get_data_sz(chk),
				    0x0, /* src */
				    fu_chunk_get_data_sz(chk),
				    error))
			return FALSE;
		blob_chunk = g_bytes_new(buf, sizeof(buf));
		if (!fu_wac_module_set_feature(self,
					       FU_WAC_MODULE_COMMAND_DATA,
					       blob_chunk,
					       fu_progress_get_child(progress),
					       FU_WAC_MODULE_WRITE_TIMEOUT,
					       error)) {
			g_prefix_error(error, "failed to write block %u: ", fu_chunk_get_idx(chk));
			return FALSE;
		}

		/* update progress */
		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						i + 1,
						chunks->len);
	}
	fu_progress_step_done(progress);

	/* end */
	if (!fu_wac_module_set_feature(self,
				       FU_WAC_MODULE_COMMAND_END,
				       NULL,
				       fu_progress_get_child(progress),
				       FU_WAC_MODULE_FINISH_TIMEOUT,
				       error))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_wac_module_touch_init(FuWacModuleTouch *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_install_duration(FU_DEVICE(self), 30);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_IHEX_FIRMWARE);
}

static void
fu_wac_module_touch_class_init(FuWacModuleTouchClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->write_firmware = fu_wac_module_touch_write_firmware;
}

FuWacModule *
fu_wac_module_touch_new(FuContext *context, GUsbDevice *usb_device)
{
	FuWacModule *module = NULL;
	module = g_object_new(FU_TYPE_WAC_MODULE_TOUCH,
			      "context",
			      context,
			      "usb-device",
			      usb_device,
			      "fw-type",
			      FU_WAC_MODULE_FW_TYPE_TOUCH,
			      NULL);
	return module;
}

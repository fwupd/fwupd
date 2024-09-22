/*
 * Copyright 2024 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-legion-hid-device.h"
#include "fu-legion-hid-struct.h"

struct _FuLegionHidDevice {
	FuHidDevice parent_instance;
};

G_DEFINE_TYPE(FuLegionHidDevice, fu_legion_hid_device, FU_TYPE_HID_DEVICE)

#define FU_LEGION_HID_DEVICE_TIMEOUT 200 /* ms */

static gboolean
fu_legion_hid_device_transfer(FuLegionHidDevice *self,
			      GByteArray *req,
			      GByteArray *res,
			      GError **error)
{
	if (req != NULL) {
		if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
					      req->data[0],
					      req->data,
					      req->len,
					      FU_LEGION_HID_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
					      error)) {
			g_prefix_error(error, "failed to send packet: ");
			return FALSE;
		}
	}
	if (res != NULL) {
		if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
					      res->data[0],
					      res->data,
					      res->len,
					      FU_LEGION_HID_DEVICE_TIMEOUT,
					      FU_HID_DEVICE_FLAG_USE_INTERRUPT_TRANSFER,
					      error)) {
			g_prefix_error(error, "failed to receive packet: ");
			return FALSE;
		}
	}
	return TRUE;
}

static gboolean
fu_legion_hid_device_ensure_version(FuLegionHidDevice *self, GError **error)
{
	for (gint i = 1; i < 5; i++) {
		g_autoptr(GByteArray) req = fu_struct_legion_hid_req_device_version_new();
		g_autoptr(GByteArray) res = fu_struct_legion_hid_res_device_version_new();

		if (i == 2)
			continue;
		fu_struct_legion_hid_req_device_version_set_device(req, i);
		if (!fu_legion_hid_device_transfer(self, req, res, error))
			return FALSE;
		if (!fu_struct_legion_hid_res_device_version_validate(res->data,
								      res->len,
								      0x0,
								      error))
			return FALSE;
		g_info("got version from device %d: 0x%x, 0x%x, 0x%x, 0x%x",
		       i,
		       fu_struct_legion_hid_res_device_version_get_ver_pro(res),
		       fu_struct_legion_hid_res_device_version_get_ver_cmd(res),
		       fu_struct_legion_hid_res_device_version_get_ver_fw(res),
		       fu_struct_legion_hid_res_device_version_get_ver_hard(res));
		// fu_device_set_version(FU_DEVICE(self), ver);
	}
	return TRUE;
}

static gboolean
fu_legion_hid_device_setup(FuDevice *device, GError **error)
{
	FuLegionHidDevice *self = FU_LEGION_HID_DEVICE(device);

	/* HidDevice->setup */
	if (!FU_DEVICE_CLASS(fu_legion_hid_device_parent_class)->setup(device, error))
		return FALSE;

	/* get the version from the hardware while open */
	if (!fu_legion_hid_device_ensure_version(self, error))
		return FALSE;

	/* success */
	return TRUE;
}

static void
fu_legion_hid_device_init(FuLegionHidDevice *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_protocol(FU_DEVICE(self), "com.legion.hid");
}

static void
fu_legion_hid_device_class_init(FuLegionHidDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_legion_hid_device_setup;
}

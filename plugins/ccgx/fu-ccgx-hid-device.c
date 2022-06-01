/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ccgx-hid-device.h"

struct _FuCcgxHidDevice {
	FuHidDevice parent_instance;
};

G_DEFINE_TYPE(FuCcgxHidDevice, fu_ccgx_hid_device, FU_TYPE_HID_DEVICE)

#define FU_CCGX_HID_DEVICE_TIMEOUT     5000 /* ms */
#define FU_CCGX_HID_DEVICE_RETRY_DELAY 30   /* ms */
#define FU_CCGX_HID_DEVICE_RETRY_CNT   5

static gboolean
fu_ccgx_hid_device_enable_hpi_mode_cb(FuDevice *device, gpointer user_data, GError **error)
{
	guint8 buf[5] = {0xEE, 0xBC, 0xA6, 0xB9, 0xA8};

	if (!fu_hid_device_set_report(FU_HID_DEVICE(device),
				      buf[0],
				      buf,
				      sizeof(buf),
				      FU_CCGX_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "switch to HPI mode error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_hid_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	if (!fu_device_retry(device,
			     fu_ccgx_hid_device_enable_hpi_mode_cb,
			     FU_CCGX_HID_DEVICE_RETRY_CNT,
			     NULL,
			     error))
		return FALSE;
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_ccgx_hid_device_setup(FuDevice *device, GError **error)
{
	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_ccgx_hid_device_parent_class)->setup(device, error))
		return FALSE;

	/* This seems insane... but we need to switch the device from HID
	 * mode to HPI mode at startup. The device continues to function
	 * exactly as before and no user-visible effects are noted */
	if (!fu_device_retry(device,
			     fu_ccgx_hid_device_enable_hpi_mode_cb,
			     FU_CCGX_HID_DEVICE_RETRY_CNT,
			     NULL,
			     error))
		return FALSE;

	/* never add this device, the daemon does not expect the device to
	 * disconnect before it is added */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "device is replugging into HPI mode");
	return FALSE;
}

static void
fu_ccgx_hid_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 98, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 2, "reload");
}

static void
fu_ccgx_hid_device_init(FuCcgxHidDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.cypress.ccgx");
	fu_device_add_protocol(FU_DEVICE(self), "com.infineon.ccgx");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_WILL_DISAPPEAR);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_retry_set_delay(FU_DEVICE(self), FU_CCGX_HID_DEVICE_RETRY_DELAY);
}

static void
fu_ccgx_hid_device_class_init(FuCcgxHidDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->detach = fu_ccgx_hid_device_detach;
	klass_device->setup = fu_ccgx_hid_device_setup;
	klass_device->set_progress = fu_ccgx_hid_device_set_progress;
}

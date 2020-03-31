/*
 * Copyright (C) 2020 Cypress Semiconductor Corporation.
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-ccgx-hid-device.h"

struct _FuCcgxHidDevice
{
	FuHidDevice		 parent_instance;
	guint			 setup_id;
};

G_DEFINE_TYPE (FuCcgxHidDevice, fu_ccgx_hid_device, FU_TYPE_HID_DEVICE)

#define FU_CCGX_HID_DEVICE_TIMEOUT	5000 /* ms */

static gboolean
fu_ccgx_hid_device_enable_hpi_mode (FuDevice *device, GError **error)
{
	guint8 buf[5] = {0xEE, 0xBC, 0xA6, 0xB9, 0xA8};

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_hid_device_set_report (FU_HID_DEVICE (device), buf[0],
				       buf, sizeof(buf),
				       FU_CCGX_HID_DEVICE_TIMEOUT,
				       FU_HID_DEVICE_FLAG_NONE,
				       error)) {
		g_prefix_error (error, "switch to HPI mode error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_hid_device_detach (FuDevice *device, GError **error)
{
	if (!fu_ccgx_hid_device_enable_hpi_mode (device, error))
		return FALSE;
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_ccgx_hid_device_setup_cb (gpointer user_data)
{
	FuCcgxHidDevice *self = FU_CCGX_HID_DEVICE (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	/* do this in idle so that the original HID device gets added */
	locker = fu_device_locker_new (self, &error);
	if (locker == NULL) {
		g_warning ("failed to open HID device: %s",
			   error->message);
	} else {
		if (!fu_ccgx_hid_device_detach (FU_DEVICE (self), &error)) {
			g_warning ("failed to detach to HPI mode: %s",
				   error->message);
		}
	}

	/* never again */
	self->setup_id = 0;
	return G_SOURCE_REMOVE;
}

static gboolean
fu_ccgx_hid_device_setup (FuDevice *device, GError **error)
{
	FuCcgxHidDevice *self = FU_CCGX_HID_DEVICE (device);
	/* This seems insane... but we need to switch the device from HID
	 * mode to HPI mode at startup. The device continues to function
	 * exactly as before and no user-visible effects are noted */
	self->setup_id = g_timeout_add (1000, fu_ccgx_hid_device_setup_cb, self);
	return TRUE;
}

static void
fu_ccgx_hid_device_finalize (GObject *object)
{
	FuCcgxHidDevice *self = FU_CCGX_HID_DEVICE (object);
	if (self->setup_id != 0)
		g_source_remove (self->setup_id);
}

static void
fu_ccgx_hid_device_init (FuCcgxHidDevice *self)
{
	fu_device_set_protocol (FU_DEVICE (self), "com.cypress.ccgx");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_WILL_DISAPPEAR);
}

static void
fu_ccgx_hid_device_class_init (FuCcgxHidDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = fu_ccgx_hid_device_finalize;
	klass_device->detach = fu_ccgx_hid_device_detach;
	klass_device->setup = fu_ccgx_hid_device_setup;
}

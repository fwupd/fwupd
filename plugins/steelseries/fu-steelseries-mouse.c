/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-steelseries-mouse.h"

#define STEELSERIES_TRANSACTION_TIMEOUT 1000 /* ms */

struct _FuSteelseriesMouse {
	FuUsbDevice parent_instance;
};

G_DEFINE_TYPE(FuSteelseriesMouse, fu_steelseries_mouse, FU_TYPE_USB_DEVICE)

static gboolean
fu_steelseries_mouse_setup(FuDevice *device, GError **error)
{
	gsize actual_len = 0;
	guint8 data[32];
	g_autofree gchar *version = NULL;

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_steelseries_mouse_parent_class)->setup(device, error))
		return FALSE;

	memset(data, 0x00, sizeof(data));
	data[0] = 0x16;
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(device),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_CLASS,
					    FU_USB_RECIPIENT_INTERFACE,
					    0x09,
					    0x0200,
					    0x0000,
					    data,
					    sizeof(data),
					    &actual_len,
					    STEELSERIES_TRANSACTION_TIMEOUT,
					    NULL,
					    error)) {
		g_prefix_error(error, "failed to do control transfer: ");
		return FALSE;
	}
	if (actual_len != 32) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "only wrote %" G_GSIZE_FORMAT "bytes",
			    actual_len);
		return FALSE;
	}
	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(device),
					      0x81, /* EP1 IN */
					      data,
					      sizeof(data),
					      &actual_len,
					      STEELSERIES_TRANSACTION_TIMEOUT,
					      NULL,
					      error)) {
		g_prefix_error(error, "failed to do EP1 transfer: ");
		return FALSE;
	}
	if (actual_len != 32) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_INVALID_DATA,
			    "only read %" G_GSIZE_FORMAT "bytes",
			    actual_len);
		return FALSE;
	}
	version = g_strdup_printf("%i.%i.%i", data[0], data[1], data[2]);
	fu_device_set_version(FU_DEVICE(device), version);

	/* success */
	return TRUE;
}

static void
fu_steelseries_mouse_init(FuSteelseriesMouse *self)
{
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), 0x00);
}

static void
fu_steelseries_mouse_class_init(FuSteelseriesMouseClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_steelseries_mouse_setup;
}

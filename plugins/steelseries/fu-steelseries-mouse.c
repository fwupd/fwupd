/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
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
	GUsbDevice *usb_device = fu_usb_device_get_dev(FU_USB_DEVICE(device));
	gboolean ret;
	gsize actual_len = 0;
	guint8 data[32];
	g_autofree gchar *version = NULL;

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_steelseries_mouse_parent_class)->setup(device, error))
		return FALSE;

	memset(data, 0x00, sizeof(data));
	data[0] = 0x16;
	ret = g_usb_device_control_transfer(usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    0x09,
					    0x0200,
					    0x0000,
					    data,
					    sizeof(data),
					    &actual_len,
					    STEELSERIES_TRANSACTION_TIMEOUT,
					    NULL,
					    error);
	if (!ret) {
		g_prefix_error(error, "failed to do control transfer: ");
		return FALSE;
	}
	if (actual_len != 32) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
			    "only wrote %" G_GSIZE_FORMAT "bytes",
			    actual_len);
		return FALSE;
	}
	ret = g_usb_device_interrupt_transfer(usb_device,
					      0x81, /* EP1 IN */
					      data,
					      sizeof(data),
					      &actual_len,
					      STEELSERIES_TRANSACTION_TIMEOUT,
					      NULL,
					      error);
	if (!ret) {
		g_prefix_error(error, "failed to do EP1 transfer: ");
		return FALSE;
	}
	if (actual_len != 32) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_INVALID_DATA,
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
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->setup = fu_steelseries_mouse_setup;
}

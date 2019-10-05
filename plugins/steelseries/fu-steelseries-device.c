/*
 * Copyright (C) 2016-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-steelseries-device.h"

#define STEELSERIES_TRANSACTION_TIMEOUT		1000 /* ms */

G_DEFINE_TYPE (FuSteelseriesDevice, fu_steelseries_device, FU_TYPE_USB_DEVICE)

static gboolean
fu_steelseries_device_open (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	const guint8 iface_idx = 0x00;

	/* get firmware version on SteelSeries Rival 100 */
	if (!g_usb_device_claim_interface (usb_device, iface_idx,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		g_prefix_error (error, "failed to claim interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_device_setup (FuDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	gboolean ret;
	gsize actual_len = 0;
	guint8 data[32];
	g_autofree gchar *version = NULL;

	memset (data, 0x00, sizeof(data));
	data[0] = 0x16;
	ret = g_usb_device_control_transfer (usb_device,
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
		g_prefix_error (error, "failed to do control transfer: ");
		return FALSE;
	}
	if (actual_len != 32) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "only wrote %" G_GSIZE_FORMAT "bytes", actual_len);
		return FALSE;
	}
	ret = g_usb_device_interrupt_transfer (usb_device,
					       0x81, /* EP1 IN */
					       data,
					       sizeof(data),
					       &actual_len,
					       STEELSERIES_TRANSACTION_TIMEOUT,
					       NULL,
					       error);
	if (!ret) {
		g_prefix_error (error, "failed to do EP1 transfer: ");
		return FALSE;
	}
	if (actual_len != 32) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_INVALID_DATA,
			     "only read %" G_GSIZE_FORMAT "bytes", actual_len);
		return FALSE;
	}
	version = g_strdup_printf ("%i.%i.%i", data[0], data[1], data[2]);
	fu_device_set_version (FU_DEVICE (device), version, FWUPD_VERSION_FORMAT_TRIPLET);

	/* success */
	return TRUE;
}

static gboolean
fu_steelseries_device_close (FuUsbDevice *device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);
	const guint8 iface_idx = 0x00;

	/* we're done here */
	if (!g_usb_device_release_interface (usb_device, iface_idx,
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     error)) {
		g_prefix_error (error, "failed to release interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_steelseries_device_init (FuSteelseriesDevice *device)
{
}

static void
fu_steelseries_device_class_init (FuSteelseriesDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_device->setup = fu_steelseries_device_setup;
	klass_usb_device->open = fu_steelseries_device_open;
	klass_usb_device->close = fu_steelseries_device_close;
}

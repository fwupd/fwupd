/*
 * Copyright (C) 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-cros-ec-usb-device.h"

G_DEFINE_TYPE (FuCrosEcUsbDevice, fu_cros_ec_usb_device, FU_TYPE_USB_DEVICE)

static gboolean
fu_cros_ec_usb_device_open (FuUsbDevice *device, GError **error)
{
	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_setup (FuDevice *device, GError **error)
{
	/* success */
	return TRUE;
}

static gboolean
fu_cros_ec_usb_device_close (FuUsbDevice *device, GError **error)
{
	/* success */
	return TRUE;
}

static void
fu_cros_ec_usb_device_init (FuCrosEcUsbDevice *device)
{
	fu_device_set_version_format (FU_DEVICE (device), FWUPD_VERSION_FORMAT_TRIPLET);
}

static void
fu_cros_ec_usb_device_class_init (FuCrosEcUsbDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_device->setup = fu_cros_ec_usb_device_setup;
	klass_usb_device->open = fu_cros_ec_usb_device_open;
	klass_usb_device->close = fu_cros_ec_usb_device_close;
}

/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuBlockDevice"

#include "config.h"

#include "fu-block-device.h"
#include "fu-usb-device.h"

/**
 * FuBlockDevice
 *
 * See also: #FuUdevDevice
 */

G_DEFINE_TYPE(FuBlockDevice, fu_block_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_block_device_probe(FuDevice *device, GError **error)
{
	g_autofree gchar *physical_id = NULL;
	g_autoptr(FuDevice) usb_device = NULL;

	/* block devices are weird in that the vendor and model are generic, use the most likely */
	usb_device = fu_device_get_backend_parent_with_subsystem(device, "usb:usb_device", error);
	if (usb_device == NULL)
		return FALSE;
	if (!fu_device_probe(usb_device, error))
		return FALSE;

	/* USB devpath as physical ID */
	physical_id = g_strdup_printf("DEVPATH=%s",
				      fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(usb_device)));
	fu_device_set_physical_id(device, physical_id);

	/* copy the VID and PID, and reconstruct compatible IDs */
	fu_udev_device_set_vendor(FU_UDEV_DEVICE(device),
				  fu_usb_device_get_vid(FU_USB_DEVICE(usb_device)));
	fu_udev_device_set_model(FU_UDEV_DEVICE(device),
				 fu_usb_device_get_pid(FU_USB_DEVICE(usb_device)));
	fu_device_add_instance_str(device, "VEN", fu_device_get_instance_str(usb_device, "VID"));
	fu_device_add_instance_str(device, "DEV", fu_device_get_instance_str(usb_device, "PID"));
	if (!fu_device_build_instance_id_full(device,
					      FU_DEVICE_INSTANCE_FLAG_QUIRKS,
					      error,
					      "BLOCK",
					      "VEN",
					      NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "BLOCK", "VEN", "DEV", NULL))
		return FALSE;

	/* but use USB as a vendor ID prefix */
	fu_device_incorporate_vendor_ids(device, usb_device);

	/* success */
	return TRUE;
}

static void
fu_block_device_init(FuBlockDevice *self)
{
}

static void
fu_block_device_class_init(FuBlockDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_block_device_probe;
}

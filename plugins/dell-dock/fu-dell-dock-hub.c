/*
 * Copyright (C) 2018 Dell Inc.
 * All rights reserved.
 *
 * This software and associated documentation (if any) is furnished
 * under a license and may only be used or copied in accordance
 * with the terms of the license.
 *
 * This file is provided under a dual MIT/LGPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 * Dell Chooses the MIT license part of Dual MIT/LGPLv2 license agreement.
 *
 * SPDX-License-Identifier: LGPL-2.1+ OR MIT
 */

#include "config.h"

#include "fu-usb-device.h"
#include "fwupd-error.h"

#include "fu-dell-dock-common.h"

struct _FuDellDockHub {
	FuUsbDevice			 parent_instance;
	guint8				 unlock_target;
	guint64				 blob_major_offset;
	guint64				 blob_minor_offset;
};

G_DEFINE_TYPE (FuDellDockHub, fu_dell_dock_hub, FU_TYPE_USB_DEVICE)

static gboolean
fu_dell_dock_hub_probe (FuDevice *device, GError **error)
{
	g_autofree gchar *devid = NULL;

	devid = g_strdup_printf ("USB\\VID_%04X&PID_%04X&hub",
			     (guint) fu_usb_device_get_vid (FU_USB_DEVICE (device)),
			     (guint) fu_usb_device_get_pid (FU_USB_DEVICE (device)));

	fu_device_set_logical_id (device, "hub");
	fu_device_add_instance_id (device, devid);
	fu_device_set_protocol (device, "com.dell.dock");

	return TRUE;
}

static gboolean
fu_dell_dock_hub_write_fw (FuDevice *device,
			   FuFirmware *firmware,
			   FwupdInstallFlags flags,
			   GError **error)
{
	FuDellDockHub *self = FU_DELL_DOCK_HUB (device);
	gsize fw_size = 0;
	const guint8 *data;
	gsize write_size;
	gsize nwritten = 0;
	guint32 address = 0;
	gboolean result = FALSE;
	g_autofree gchar *dynamic_version = NULL;
	g_autoptr(GBytes) fw = NULL;

	g_return_val_if_fail (device != NULL, FALSE);
	g_return_val_if_fail (FU_IS_FIRMWARE (firmware), FALSE);

	/* get default image */
	fw = fu_firmware_get_image_default_bytes (firmware, error);
	if (fw == NULL)
		return FALSE;
	data = g_bytes_get_data (fw, &fw_size);
	write_size = (fw_size / HIDI2C_MAX_WRITE) >= 1 ?
				 HIDI2C_MAX_WRITE : fw_size;

	dynamic_version = g_strdup_printf ("%02x.%02x",
					   data[self->blob_major_offset],
					   data[self->blob_minor_offset]);
	g_debug ("writing hub firmware version %s", dynamic_version);

	if (!fu_dell_dock_set_power (device, self->unlock_target, TRUE, error))
		return FALSE;

	if (!fu_dell_dock_hid_raise_mcu_clock (device, TRUE, error))
		return FALSE;

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
	if (!fu_dell_dock_hid_erase_bank (device, 1, error))
		return FALSE;

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	do {
		/* last packet */
		if (fw_size - nwritten < write_size)
			write_size = fw_size - nwritten;

		if (!fu_dell_dock_hid_write_flash (device, address, data,
						   write_size, error))
			return FALSE;
		nwritten += write_size;
		data += write_size;
		address += write_size;
		fu_device_set_progress_full (device, nwritten, fw_size);
	} while (nwritten < fw_size);

	fu_device_set_status (device, FWUPD_STATUS_DEVICE_BUSY);
	if (!fu_dell_dock_hid_verify_update (device, &result, error))
		return FALSE;
	if (!result) {
		g_set_error_literal (error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL,
				     "Failed to verify the update");
		return FALSE;
	}

	/* dock will reboot to re-read; this is to appease the daemon */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_set_version (device, dynamic_version, FWUPD_VERSION_FORMAT_PAIR);
	return TRUE;
}

static gboolean
fu_dell_dock_hub_set_quirk_kv (FuDevice *device,
			       const gchar *key,
			       const gchar *value,
			       GError **error)
{
	FuDellDockHub *self = FU_DELL_DOCK_HUB (device);

	if (g_strcmp0 (key, "DellDockUnlockTarget") == 0) {
		guint64 tmp = fu_common_strtoull (value);
		if (tmp < G_MAXUINT8) {
			self->unlock_target = tmp;
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "invalid DellDockUnlockTarget");
		return FALSE;
	}
	if (g_strcmp0 (key, "DellDockBlobMajorOffset") == 0) {
		self->blob_major_offset = fu_common_strtoull (value);
		return TRUE;
	}
	if (g_strcmp0 (key, "DellDockBlobMinorOffset") == 0) {
		self->blob_minor_offset = fu_common_strtoull (value);
		return TRUE;
	}

	/* failed */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "quirk key not supported");
	return FALSE;
}

static gboolean
fu_dell_dock_hub_open (FuUsbDevice *fu_usb_device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (fu_usb_device);

	/* open device and clear status */
	if (!g_usb_device_claim_interface (
		usb_device, 0, /* HID */
		G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER, error)) {
		g_prefix_error (error, "failed to claim HID interface: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_dell_dock_hub_close (FuUsbDevice *fu_usb_device, GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (fu_usb_device);

	if (!g_usb_device_release_interface (
		usb_device, 0, /* HID */
		G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER, error)) {
		g_prefix_error (error, "failed to release interface: ");
		return FALSE;
	}

	return TRUE;
}

static void
fu_dell_dock_hub_finalize (GObject *object)
{
	G_OBJECT_CLASS (fu_dell_dock_hub_parent_class)->finalize (object);
}

static void
fu_dell_dock_hub_init (FuDellDockHub *self)
{
}

static void
fu_dell_dock_hub_class_init (FuDellDockHubClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	object_class->finalize = fu_dell_dock_hub_finalize;
	klass_usb_device->open = fu_dell_dock_hub_open;
	klass_usb_device->close = fu_dell_dock_hub_close;
	klass_device->setup = fu_dell_dock_hid_get_hub_version;
	klass_device->probe = fu_dell_dock_hub_probe;
	klass_device->write_firmware = fu_dell_dock_hub_write_fw;
	klass_device->set_quirk_kv = fu_dell_dock_hub_set_quirk_kv;
}

FuDellDockHub *
fu_dell_dock_hub_new (FuUsbDevice *device)
{
	FuDellDockHub *self = g_object_new (FU_TYPE_DELL_DOCK_HUB, NULL);
	fu_device_incorporate (FU_DEVICE (self), FU_DEVICE (device));
	return self;
}

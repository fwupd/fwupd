/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-hailuck-common.h"
#include "fu-hailuck-kbd-device.h"
#include "fu-hailuck-tp-device.h"

struct _FuHailuckKbdDevice {
	FuHidDevice		 parent_instance;
};

G_DEFINE_TYPE (FuHailuckKbdDevice, fu_hailuck_kbd_device, FU_TYPE_HID_DEVICE)

static gboolean
fu_hailuck_kbd_device_detach (FuDevice *device, GError **error)
{
	guint8 buf[6] = {
		FU_HAILUCK_REPORT_ID_SHORT,
		FU_HAILUCK_CMD_DETACH
	};
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	if (!fu_hid_device_set_report (FU_HID_DEVICE (device),
				       buf[0], buf, sizeof(buf), 1000,
				       FU_HID_DEVICE_FLAG_IS_FEATURE, error))
		return FALSE;
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_hailuck_kbd_device_probe (FuDevice *device, GError **error)
{
	g_autofree gchar *devid = NULL;
	g_autoptr(FuHailuckTpDevice) tp_device = fu_hailuck_tp_device_new (FU_DEVICE (device));

	/* FuUsbDevice->probe */
	if (!FU_DEVICE_CLASS (fu_hailuck_kbd_device_parent_class)->probe (device, error))
		return FALSE;

	/* add extra keyboard-specific GUID */
	devid = g_strdup_printf ("USB\\VID_%04X&PID_%04X&MODE_KBD",
				 fu_usb_device_get_vid (FU_USB_DEVICE (device)),
				 fu_usb_device_get_pid (FU_USB_DEVICE (device)));
	fu_device_add_instance_id (device, devid);

	/* add touchpad */
	if (!fu_device_probe (FU_DEVICE (tp_device), error))
		return FALSE;

	/* assume the TP has the same version as the keyboard */
	fu_device_set_version (FU_DEVICE (tp_device),
			       fu_device_get_version (device));
	fu_device_set_version_format (FU_DEVICE (tp_device),
				      fu_device_get_version_format (device));
	fu_device_add_child (device, FU_DEVICE (tp_device));

	/* success */
	return TRUE;
}

static void
fu_hailuck_kbd_device_init (FuHailuckKbdDevice *self)
{
	fu_device_set_firmware_size (FU_DEVICE (self), 0x4000);
	fu_device_set_protocol (FU_DEVICE (self), "com.hailuck.kbd");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_internal_flag (FU_DEVICE (self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_icon (FU_DEVICE (self), "input-keyboard");
	fu_hid_device_set_interface (FU_HID_DEVICE (self), 0x1);
	fu_device_set_remove_delay (FU_DEVICE (self),
				    FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
}

static void
fu_hailuck_kbd_device_class_init (FuHailuckKbdDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	klass_device->detach = fu_hailuck_kbd_device_detach;
	klass_device->probe = fu_hailuck_kbd_device_probe;
}

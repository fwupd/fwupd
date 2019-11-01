/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-firmware-common.h"

#include "fu-jabra-device.h"

struct _FuJabraDevice {
	FuUsbDevice			 parent_instance;
	gchar				*magic;
};

G_DEFINE_TYPE (FuJabraDevice, fu_jabra_device, FU_TYPE_USB_DEVICE)

static void
fu_jabra_device_to_string (FuDevice *device, guint idt, GString *str)
{
	FuJabraDevice *self = FU_JABRA_DEVICE (device);
	fu_common_string_append_kv (str, idt, "Magic", self->magic);
}

static guint8
_g_usb_device_get_interface_for_class (GUsbDevice *dev,
				       guint8 intf_class,
				       GError **error)
{
	g_autoptr(GPtrArray) intfs = NULL;
	intfs = g_usb_device_get_interfaces (dev, error);
	if (intfs == NULL)
		return 0xff;
	for (guint i = 0; i < intfs->len; i++) {
		GUsbInterface *intf = g_ptr_array_index (intfs, i);
		if (g_usb_interface_get_class (intf) == intf_class)
			return g_usb_interface_get_number (intf);
	}
	return 0xff;
}

/* slightly weirdly, this magic turns the device into appIDLE, so we
 * need the DFU plugin to further detach us into dfuIDLE */
static gboolean
fu_jabra_device_prepare (FuDevice *device, FwupdInstallFlags flags, GError **error)
{
	FuJabraDevice *self = FU_JABRA_DEVICE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (device));
	guint8 adr = 0x00;
	guint8 rep = 0x00;
	guint8 iface_hid;
	guint8 buf[33] = { 0x00 };
	g_autoptr(GError) error_local = NULL;

	/* parse string and create magic packet */
	rep = fu_firmware_strparse_uint8 (self->magic + 0);
	adr = fu_firmware_strparse_uint8 (self->magic + 2);
	buf[0] = rep;
	buf[1] = adr;
	buf[2] = 0x00;
	buf[3] = 0x01;
	buf[4] = 0x85;
	buf[5] = 0x07;

	/* detach the HID interface from the kernel driver */
	iface_hid = _g_usb_device_get_interface_for_class (usb_device,
							   G_USB_DEVICE_CLASS_HID,
							   &error_local);
	if (iface_hid == 0xff) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot find HID interface: %s",
			     error_local->message);
		return FALSE;
	}
	g_debug ("claiming interface 0x%02x", iface_hid);
	if (!g_usb_device_claim_interface (usb_device, (gint) iface_hid,
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   &error_local)) {
		g_set_error (error,
			     FWUPD_ERROR,
			     FWUPD_ERROR_NOT_SUPPORTED,
			     "cannot claim interface 0x%02x: %s",
			     iface_hid, error_local->message);
		return FALSE;
	}

	/* send magic to device */
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    0x09,
					    0x0200 | rep,
					    0x0003,
					    buf, 33, NULL,
					    FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE,
					    NULL, /* cancellable */
					    &error_local)) {
		g_debug ("whilst sending magic: %s, ignoring",
			 error_local->message);
	}

	/* wait for device to re-appear and be added to the dfu plugin */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_add_flag (device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_jabra_device_set_quirk_kv (FuDevice *device,
			      const gchar *key,
			      const gchar *value,
			      GError **error)
{
	FuJabraDevice *self = FU_JABRA_DEVICE (device);

	if (g_strcmp0 (key, "JabraMagic") == 0) {
		if (value != NULL && strlen (value) == 4) {
			self->magic = g_strdup (value);
			return TRUE;
		}
		g_set_error_literal (error,
				     G_IO_ERROR,
				     G_IO_ERROR_INVALID_DATA,
				     "unsupported jabra quirk format");
		return FALSE;
	}

	/* failed */
	g_set_error_literal (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_SUPPORTED,
			     "quirk key not supported");
	return FALSE;
}

static void
fu_jabra_device_init (FuJabraDevice *self)
{
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_set_remove_delay (FU_DEVICE (self), 20000); /* 10+10s! */
}

static void
fu_jabra_device_finalize (GObject *object)
{
	FuJabraDevice *self = FU_JABRA_DEVICE (object);
	g_free (self->magic);
	G_OBJECT_CLASS (fu_jabra_device_parent_class)->finalize (object);
}

static void
fu_jabra_device_class_init (FuJabraDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	object_class->finalize = fu_jabra_device_finalize;
	klass_device->to_string = fu_jabra_device_to_string;
	klass_device->prepare = fu_jabra_device_prepare;
	klass_device->set_quirk_kv = fu_jabra_device_set_quirk_kv;
}

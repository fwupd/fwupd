/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "fu-jabra-device.h"

struct _FuJabraDevice {
	FuUsbDevice parent_instance;
	gchar *magic;
};

G_DEFINE_TYPE(FuJabraDevice, fu_jabra_device, FU_TYPE_USB_DEVICE)

static void
fu_jabra_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuJabraDevice *self = FU_JABRA_DEVICE(device);
	fwupd_codec_string_append(str, idt, "Magic", self->magic);
}

static guint8
_fu_usb_device_get_interface_for_class(FuUsbDevice *usb_device, guint8 intf_class, GError **error)
{
	g_autoptr(GPtrArray) intfs = NULL;
	intfs = fu_usb_device_get_interfaces(usb_device, error);
	if (intfs == NULL)
		return 0xff;
	for (guint i = 0; i < intfs->len; i++) {
		FuUsbInterface *intf = g_ptr_array_index(intfs, i);
		if (fu_usb_interface_get_class(intf) == intf_class)
			return fu_usb_interface_get_number(intf);
	}
	return 0xff;
}

/* slightly weirdly, this magic turns the device into appIDLE, so we
 * need the DFU plugin to further detach us into dfuIDLE */
static gboolean
fu_jabra_device_prepare(FuDevice *device,
			FuProgress *progress,
			FwupdInstallFlags flags,
			GError **error)
{
	FuJabraDevice *self = FU_JABRA_DEVICE(device);
	gsize magiclen = strlen(self->magic);
	guint8 adr = 0x00;
	guint8 rep = 0x00;
	guint8 iface_hid;
	guint8 buf[33] = {0x00};
	g_autoptr(GError) error_local = NULL;

	/* parse string and create magic packet */
	if (!fu_firmware_strparse_uint8_safe(self->magic, magiclen, 0, &rep, error))
		return FALSE;
	if (!fu_firmware_strparse_uint8_safe(self->magic, magiclen, 2, &adr, error))
		return FALSE;
	buf[0] = rep;
	buf[1] = adr;
	buf[2] = 0x00;
	buf[3] = 0x01;
	buf[4] = 0x85;
	buf[5] = 0x07;

	/* detach the HID interface from the kernel driver */
	iface_hid = _fu_usb_device_get_interface_for_class(FU_USB_DEVICE(self),
							   FU_USB_CLASS_HID,
							   &error_local);
	if (iface_hid == 0xff) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot find HID interface: %s",
			    error_local->message);
		return FALSE;
	}
	g_debug("claiming interface 0x%02x", iface_hid);
	if (!fu_usb_device_claim_interface(FU_USB_DEVICE(self),
					   (gint)iface_hid,
					   FU_USB_DEVICE_CLAIM_FLAG_KERNEL_DRIVER,
					   &error_local)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "cannot claim interface 0x%02x: %s",
			    iface_hid,
			    error_local->message);
		return FALSE;
	}

	/* send magic to device */
	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_CLASS,
					    FU_USB_RECIPIENT_INTERFACE,
					    0x09,
					    0x0200 | rep,
					    0x0003,
					    buf,
					    33,
					    NULL,
					    FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE,
					    NULL, /* cancellable */
					    &error_local)) {
		g_debug("whilst sending magic: %s, ignoring", error_local->message);
	}

	/* wait for device to re-appear and be added to the dfu plugin */
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_RESTART);
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_jabra_device_set_quirk_kv(FuDevice *device, const gchar *key, const gchar *value, GError **error)
{
	FuJabraDevice *self = FU_JABRA_DEVICE(device);

	if (g_strcmp0(key, "JabraMagic") == 0) {
		if (value != NULL && strlen(value) == 4) {
			self->magic = g_strdup(value);
			return TRUE;
		}
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INVALID_DATA,
				    "unsupported jabra quirk format");
		return FALSE;
	}

	/* failed */
	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "quirk key not supported");
	return FALSE;
}

static void
fu_jabra_device_init(FuJabraDevice *self)
{
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_set_remove_delay(FU_DEVICE(self), 20000); /* 10+10s! */
	fu_device_add_protocol(FU_DEVICE(self), "org.usb.dfu");
}

static void
fu_jabra_device_finalize(GObject *object)
{
	FuJabraDevice *self = FU_JABRA_DEVICE(object);
	g_free(self->magic);
	G_OBJECT_CLASS(fu_jabra_device_parent_class)->finalize(object);
}

static void
fu_jabra_device_class_init(FuJabraDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_jabra_device_finalize;
	device_class->to_string = fu_jabra_device_to_string;
	device_class->prepare = fu_jabra_device_prepare;
	device_class->set_quirk_kv = fu_jabra_device_set_quirk_kv;
}

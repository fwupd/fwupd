/*
 * Copyright 2024 Chris Hofstaedtler <ch@zeha.at>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-rp-pico-device.h"
#include "fu-rp-pico-struct.h"

struct _FuRpPicoDevice {
	FuUsbDevice parent_instance;
	guint8 iface_reset;
};

G_DEFINE_TYPE(FuRpPicoDevice, fu_rp_pico_device, FU_TYPE_USB_DEVICE)

#define FU_RP_PICO_DEVICE_RESET_INTERFACE_SUBCLASS 0x00
#define FU_RP_PICO_DEVICE_RESET_INTERFACE_PROTOCOL 0x01

static void
fu_rp_pico_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuRpPicoDevice *self = FU_RP_PICO_DEVICE(device);
	fwupd_codec_string_append_hex(str, idt, "IfaceReset", self->iface_reset);
}

static gboolean
fu_rp_pico_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuRpPicoDevice *self = FU_RP_PICO_DEVICE(device);
	g_autoptr(GError) error_local = NULL;

	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_CLASS,
					    FU_USB_RECIPIENT_INTERFACE,
					    FU_RP_PICO_RESET_REQUEST_BOOTSEL,
					    0,
					    self->iface_reset,
					    NULL,
					    0,
					    NULL,
					    2000,
					    NULL,
					    &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_READ) ||
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND)) {
			g_debug("ignoring expected error %s", error_local->message);
		} else {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to restart device: ");
			return FALSE;
		}
	}

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_rp_pico_device_probe(FuDevice *device, GError **error)
{
	FuRpPicoDevice *self = FU_RP_PICO_DEVICE(device);
	g_autoptr(FuUsbInterface) intf = NULL;

	intf = fu_usb_device_get_interface(FU_USB_DEVICE(self),
					   FU_USB_CLASS_VENDOR_SPECIFIC,
					   FU_RP_PICO_DEVICE_RESET_INTERFACE_SUBCLASS,
					   FU_RP_PICO_DEVICE_RESET_INTERFACE_PROTOCOL,
					   error);
	if (intf == NULL)
		return FALSE;
	self->iface_reset = fu_usb_interface_get_number(intf);
	fu_usb_device_add_interface(FU_USB_DEVICE(self), self->iface_reset);
	return TRUE;
}

static void
fu_rp_pico_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 15, "reload");
}

static void
fu_rp_pico_device_init(FuRpPicoDevice *self)
{
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.microsoft.uf2");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN);
	/* revisions indicate incompatible hardware */
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_INSTANCE_ID_REV);
	fu_device_retry_set_delay(FU_DEVICE(self), 100);
}

static void
fu_rp_pico_device_class_init(FuRpPicoDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->to_string = fu_rp_pico_device_to_string;
	device_class->probe = fu_rp_pico_device_probe;
	device_class->detach = fu_rp_pico_device_detach;
	device_class->set_progress = fu_rp_pico_device_set_progress;
}

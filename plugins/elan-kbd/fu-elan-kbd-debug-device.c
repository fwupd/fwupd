/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-kbd-debug-device.h"
#include "fu-elan-kbd-struct.h"

struct _FuElanKbdDebugDevice {
	FuUsbDevice parent_instance;
};

G_DEFINE_TYPE(FuElanKbdDebugDevice, fu_elan_kbd_debug_device, FU_TYPE_USB_DEVICE)

static gboolean
fu_elan_kbd_debug_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuElanKbdDebugDevice *self = FU_ELAN_KBD_DEBUG_DEVICE(device);
	g_autoptr(GError) error_local = NULL;
	guint8 buf[8] = {0x01};

	if (!fu_usb_device_interrupt_transfer(FU_USB_DEVICE(self),
					      buf[0],
					      buf,
					      sizeof(buf),
					      NULL,
					      1000,
					      NULL,
					      &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_NOT_FOUND) ||
		    g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_READ)) {
			g_debug("ignoring: %s", error_local->message);
		} else {
			g_propagate_error(error, g_steal_pointer(&error_local));
			return FALSE;
		}
	}

	/* success */
	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static void
fu_elan_kbd_debug_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 57, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 43, "reload");
}

static void
fu_elan_kbd_debug_device_init(FuElanKbdDebugDevice *self)
{
	fu_device_set_name(FU_DEVICE(self), "ELAN USB Keyboard (debug)");
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.elan.kbd");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_icon(FU_DEVICE(self), "input-keyboard");
}

static void
fu_elan_kbd_debug_device_class_init(FuElanKbdDebugDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->detach = fu_elan_kbd_debug_device_detach;
	device_class->set_progress = fu_elan_kbd_debug_device_set_progress;
}

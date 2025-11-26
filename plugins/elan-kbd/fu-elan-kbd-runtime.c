/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-elan-kbd-firmware.h"
#include "fu-elan-kbd-runtime.h"
#include "fu-elan-kbd-struct.h"

struct _FuElanKbdRuntime {
	FuHidDevice parent_instance;
};

G_DEFINE_TYPE(FuElanKbdRuntime, fu_elan_kbd_runtime, FU_TYPE_HID_DEVICE)

static gboolean
fu_elan_kbd_runtime_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuElanKbdRuntime *self = FU_ELAN_KBD_RUNTIME(device);
	g_autoptr(GError) error_local = NULL;
	guint8 buf[8] = {
	    0xBC, /* enter IAP */
	    0x01,
	};
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      buf[0],
				      buf,
				      sizeof(buf),
				      1000,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
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
fu_elan_kbd_runtime_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 19, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 47, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 30, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 3, "reload");
}

static void
fu_elan_kbd_runtime_init(FuElanKbdRuntime *self)
{
	fu_device_set_name(FU_DEVICE(self), "ELAN USB Keyboard");
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.elan.kbd");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_PARENT_NAME_PREFIX);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_INPUT_KEYBOARD);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_ELAN_KBD_FIRMWARE);
	fu_device_add_instance_id_full(FU_DEVICE(self),
				       "USB\\VID_04F3&PID_0905",
				       FU_DEVICE_INSTANCE_FLAG_COUNTERPART);
}

static void
fu_elan_kbd_runtime_class_init(FuElanKbdRuntimeClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->detach = fu_elan_kbd_runtime_detach;
	device_class->set_progress = fu_elan_kbd_runtime_set_progress;
}

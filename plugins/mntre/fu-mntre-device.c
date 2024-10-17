/*
 * Copyright 2024 Chris hofstaedtler <Ch@zeha.at>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <stdio.h>

#include "fu-mntre-device.h"
#include "mntre_reset.h"

struct _FuMntreDevice {
	FuUsbDevice parent_instance;
};

G_DEFINE_TYPE(FuMntreDevice, fu_mntre_device, FU_TYPE_USB_DEVICE)

static gboolean
fu_mntre_device_get_firmware_version(FuMntreDevice *self,
				     GError **error,
				     struct mntre_reset_firmware_version *firmware_version)
{
	g_autoptr(GError) error_local = NULL;
	gsize actual_len = 0;

	guint16 reset_interface = 2; // TODO

	FuUsbDeviceClaimFlags flags = 0;

	if (!fu_usb_device_claim_interface(FU_USB_DEVICE(self), reset_interface, flags, error)) {
		g_prefix_error(error, "failed to claim HID interface: ");
		return FALSE;
	}

	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_DEVICE_TO_HOST,
					    FU_USB_REQUEST_TYPE_CLASS,
					    FU_USB_RECIPIENT_INTERFACE,
					    MNTRE_RESET_GET_FIRMWARE_VERSION,
					    0,
					    reset_interface,
					    (guint8 *)firmware_version,
					    sizeof(*firmware_version),
					    &actual_len,
					    2000,
					    NULL,
					    &error_local)) {
		g_propagate_prefixed_error(error,
					   g_steal_pointer(&error_local),
					   "failed to read firmware version: ");
		return FALSE;
	}

	return TRUE;
}

static gboolean
fu_mntre_device_reset_into_bootsel(FuMntreDevice *self, GError **error)
{
	g_autoptr(GError) error_local = NULL;

	guint16 reset_interface = 2; // TODO

	FuUsbDeviceClaimFlags flags = 0;

	if (!fu_usb_device_claim_interface(FU_USB_DEVICE(self), reset_interface, flags, error)) {
		g_prefix_error(error, "failed to claim HID interface: ");
		return FALSE;
	}

	if (!fu_usb_device_control_transfer(FU_USB_DEVICE(self),
					    FU_USB_DIRECTION_HOST_TO_DEVICE,
					    FU_USB_REQUEST_TYPE_CLASS,
					    FU_USB_RECIPIENT_INTERFACE,
					    MNTRE_RESET_REQUEST_BOOTSEL,
					    0,
					    reset_interface,
					    NULL,
					    0,
					    NULL,
					    2000,
					    NULL,
					    &error_local)) {
		if (g_error_matches(error_local, FWUPD_ERROR, FWUPD_ERROR_READ)) {
			g_debug("ignoring expected error %s", error_local->message);
		} else {
			g_propagate_prefixed_error(error,
						   g_steal_pointer(&error_local),
						   "failed to restart device: ");
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
fu_mntre_device_detach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuMntreDevice *self = FU_MNTRE_DEVICE(device);

	if (!fu_mntre_device_reset_into_bootsel(self, error))
		return FALSE;

	fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	return TRUE;
}

static gboolean
fu_mntre_device_ensure_version(FuMntreDevice *self, GError **error)
{
	g_autofree gchar *version = NULL;
	struct mntre_reset_firmware_version firmware_version = {};

	if (!fu_mntre_device_get_firmware_version(self, error, &firmware_version))
		return FALSE;

	version = g_strdup_printf("%08u", firmware_version.patch);

	fu_device_set_version(FU_DEVICE(self), version);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_NUMBER);

	return TRUE;
}

static gboolean
fu_mntre_device_setup(FuDevice *device, GError **error)
{
	FuMntreDevice *self = FU_MNTRE_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_mntre_device_parent_class)->setup(device, error)) {
		return FALSE;
	}

	if (!fu_mntre_device_ensure_version(self, error)) {
		return FALSE;
	}

	/* success */
	return TRUE;
}

static void
fu_mntre_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 80, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 15, "reload");
}

static void
fu_mntre_device_init(FuMntreDevice *self)
{
	fu_device_set_remove_delay(FU_DEVICE(self), FU_DEVICE_REMOVE_DELAY_RE_ENUMERATE);
	fu_device_add_protocol(FU_DEVICE(self), "com.microsoft.uf2");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ADD_COUNTERPART_GUIDS);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REPLUG_MATCH_GUID);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_RETRY_OPEN);
	fu_device_add_counterpart_guid(FU_DEVICE(self), "BLOCK\\VEN_2E8A&DEV_0003");
	fu_device_retry_set_delay(FU_DEVICE(self), 100);
}

static void
fu_mntre_device_class_init(FuMntreDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->setup = fu_mntre_device_setup;
	device_class->detach = fu_mntre_device_detach;
	device_class->set_progress = fu_mntre_device_set_progress;
}

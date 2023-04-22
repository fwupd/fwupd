/*
 * Copyright (c) 1999-2023 Logitech, Inc.
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-logitech-tap-device.h"

G_DEFINE_TYPE(FuLogitechTapDevice, fu_logitech_tap_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_logitech_tap_device_write_firmware(FuDevice *device,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuLogitechTapDeviceClass *klass = FU_LOGITECH_TAP_DEVICE_GET_CLASS(device);
	g_autofree gchar *old_firmware_version = NULL;
	g_autoptr(GBytes) fw = NULL;
	g_autoptr(GPtrArray) chunks = NULL;

	/* save the current firmware version for troubleshooting purpose */
	old_firmware_version = g_strdup(fu_device_get_version(device));

	g_debug("update %s firmware", old_firmware_version);

	/* progress */
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	
	/* get image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	
	fu_progress_set_status(progress, FWUPD_STATUS_DEVICE_WRITE);
	chunks = fu_chunk_array_new_from_bytes(fw, 0x0, 0x0, 32);

	/* write */
	if ((klass->write_firmware != NULL) && (!klass->write_firmware(device,
						chunks,
						fu_progress_get_child(progress),
						error)))
		return FALSE;
	fu_progress_step_done(progress);

	/* success */
	return TRUE;
}

static void
fu_logitech_tap_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_logitech_tap_device_init(FuLogitechTapDevice *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "logitech.hardware.tap");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
}

static void
fu_logitech_tap_device_finalize(GObject *object)
{
	G_OBJECT_CLASS(fu_logitech_tap_device_parent_class)->finalize(object);
}

static void
fu_logitech_tap_device_class_init(FuLogitechTapDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_logitech_tap_device_finalize;
	klass_device->write_firmware = fu_logitech_tap_device_write_firmware;
	klass_device->set_progress = fu_logitech_tap_device_set_progress;
}

/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-logitech-bulkcontroller-child.h"

struct _FuLogitechBulkcontrollerChild {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuLogitechBulkcontrollerChild, fu_logitech_bulkcontroller_child, FU_TYPE_DEVICE)

static gboolean
fu_logitech_bulkcontroller_child_write_firmware(FuDevice *device,
						FuFirmware *firmware,
						FuProgress *progress,
						FwupdInstallFlags flags,
						GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	/*
	 * set the flag, to let parent know that firmware update is for child, no need to wait for
	 * replug event, after child firmware is updated
	 *
	 */
	fu_device_add_private_flag(proxy,
				   FU_LOGITECH_BULKCONTROLLER_DEVICE_FLAG_PHERIPHERAL_UPDATE);
	return fu_device_write_firmware(proxy, firmware, progress, flags, error);
}

static void
fu_logitech_bulkcontroller_child_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 10, "reload");
}

static void
fu_logitech_bulkcontroller_child_init(FuLogitechBulkcontrollerChild *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.logitech.vc.proto");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_WEB_CAMERA);
}

static void
fu_logitech_bulkcontroller_child_class_init(FuLogitechBulkcontrollerChildClass *klass)
{
	FuDeviceClass *child_class = FU_DEVICE_CLASS(klass);
	child_class->write_firmware = fu_logitech_bulkcontroller_child_write_firmware;
	child_class->set_progress = fu_logitech_bulkcontroller_child_set_progress;
}

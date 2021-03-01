/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-flashrom-device.h"

struct _FuFlashromDevice {
	FuDevice		 parent_instance;
};

G_DEFINE_TYPE (FuFlashromDevice, fu_flashrom_device, FU_TYPE_DEVICE)

static void
fu_flashrom_device_init (FuFlashromDevice *self)
{
	fu_device_add_protocol (FU_DEVICE (self), "org.flashrom");
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_NEEDS_REBOOT);
	fu_device_add_flag (FU_DEVICE (self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_instance_id (FU_DEVICE (self), "main-system-firmware");
	fu_device_add_internal_flag (FU_DEVICE (self), FU_DEVICE_INTERNAL_FLAG_ENSURE_SEMVER);
	fu_device_set_physical_id (FU_DEVICE (self), "flashrom");
	fu_device_set_logical_id (FU_DEVICE (self), "bios");
	fu_device_set_version_format (FU_DEVICE (self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_icon (FU_DEVICE (self), "computer");
}

static void
fu_flashrom_device_class_init (FuFlashromDeviceClass *klass)
{
}

FuDevice *
fu_flashrom_device_new (void)
{
	return FU_DEVICE (g_object_new (FU_TYPE_FLASHROM_DEVICE, NULL));
}

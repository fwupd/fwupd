/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-test-target-device.h"

struct _FuTestTargetDevice {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuTestTargetDevice, fu_test_target_device, FU_TYPE_DEVICE)

static void
fu_test_target_device_set_progress(FuDevice *device, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static gboolean
fu_test_target_device_probe(FuDevice *device, GError **error)
{
	fu_device_add_instance_id(device, "4b0a5d4e-2c8f-4f3a-9d1e-6a7b8c9d0e1f");
	return TRUE;
}

static void
fu_test_target_device_init(FuTestTargetDevice *self)
{
	fu_device_set_id(FU_DEVICE(self), "FakeTargetDevice");
	fu_device_set_name(FU_DEVICE(self), "SPI Flash");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_MEMORY);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_IS_PROGRAMMER_TARGET);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_IGNORE_SYSTEM_POWER);
	fu_device_add_protocol(FU_DEVICE(self), "com.acme.test");
	fu_device_set_summary(FU_DEVICE(self), "Fake flash chip connected to a programmer");
	fu_device_set_vendor(FU_DEVICE(self), "ACME Corp.");
	fu_device_build_vendor_id_u16(FU_DEVICE(self), "USB", 0x1A86);
}

static void
fu_test_target_device_class_init(FuTestTargetDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->set_progress = fu_test_target_device_set_progress;
	device_class->probe = fu_test_target_device_probe;
}

FuDevice *
fu_test_target_device_new(FuContext *ctx)
{
	return g_object_new(FU_TYPE_TEST_TARGET_DEVICE, "context", ctx, NULL);
}

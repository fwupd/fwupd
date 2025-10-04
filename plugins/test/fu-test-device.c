/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-test-device.h"

struct _FuTestDevice {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuTestDevice, fu_test_device, FU_TYPE_DEVICE)

static void
fu_test_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 1, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 3, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 33, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 3, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 61, "reload");
}

static gboolean
fu_test_device_probe(FuDevice *device, GError **error)
{
	fu_device_add_instance_id(device, "b585990a-003e-5270-89d5-3705a17f9a43");
	return TRUE;
}

static void
fu_test_device_init(FuTestDevice *self)
{
	fu_device_set_id(FU_DEVICE(self), "FakeDevice");
	fu_device_set_name(FU_DEVICE(self), "Integrated_Webcam(TM)");
	fu_device_add_icon(FU_DEVICE(self), FU_DEVICE_ICON_WEB_CAMERA);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_REQUIRE_AC);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_VERIFY_IMAGE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_CAN_EMULATION_TAG);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_IGNORE_SYSTEM_POWER);
	fu_device_add_request_flag(FU_DEVICE(self), FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fu_device_add_protocol(FU_DEVICE(self), "com.acme.test");
	fu_device_set_summary(FU_DEVICE(self), "Fake webcam");
	fu_device_set_vendor(FU_DEVICE(self), "ACME Corp.");
	fu_device_build_vendor_id_u16(FU_DEVICE(self), "USB", 0x046D);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_set_version_bootloader(FU_DEVICE(self), "0.1.2");
	fu_device_set_version(FU_DEVICE(self), "1.2.2");
	fu_device_set_version_lowest(FU_DEVICE(self), "1.2.0");
}

static void
fu_test_device_class_init(FuTestDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->set_progress = fu_test_device_set_progress;
	device_class->probe = fu_test_device_probe;
}

FuDevice *
fu_test_device_new(FuContext *ctx)
{
	return g_object_new(FU_TYPE_TEST_DEVICE, "context", ctx, NULL);
}

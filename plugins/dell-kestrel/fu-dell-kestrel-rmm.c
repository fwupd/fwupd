/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-kestrel-common.h"

struct _FuDellKestrelRmm {
	FuDellKestrelHidDevice parent_instance;
};

G_DEFINE_TYPE(FuDellKestrelRmm, fu_dell_kestrel_rmm, FU_TYPE_DELL_KESTREL_HID_DEVICE)

static gchar *
fu_dell_kestrel_rmm_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32_hex(version_raw, fu_device_get_version_format(device));
}

void
fu_dell_kestrel_rmm_fix_version(FuDellKestrelRmm *self)
{
	FuDevice *parent = NULL;

	/* use version given by parent */
	parent = fu_device_get_parent(FU_DEVICE(self));
	if (parent != NULL) {
		guint32 rmm_version;
		rmm_version = fu_dell_kestrel_ec_get_rmm_version(FU_DELL_KESTREL_EC(parent));
		fu_device_set_version_raw(FU_DEVICE(self), rmm_version);
	}
}

static gboolean
fu_dell_kestrel_rmm_setup(FuDevice *device, GError **error)
{
	FuDellKestrelRmm *self = FU_DELL_KESTREL_RMM(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_dell_kestrel_rmm_parent_class)->setup(device, error))
		return FALSE;

	fu_dell_kestrel_rmm_fix_version(self);
	return TRUE;
}

static gboolean
fu_dell_kestrel_rmm_write(FuDevice *device,
			  FuFirmware *firmware,
			  FuProgress *progress,
			  FwupdInstallFlags flags,
			  GError **error)
{
	return fu_dell_kestrel_hid_device_write_firmware(FU_DELL_KESTREL_HID_DEVICE(device),
							 firmware,
							 progress,
							 FU_DELL_KESTREL_EC_DEV_TYPE_RMM,
							 0,
							 error);
}

static void
fu_dell_kestrel_rmm_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 13, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 72, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 9, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 7, "reload");
}

static void
fu_dell_kestrel_rmm_init(FuDellKestrelRmm *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.dell.kestrel");
	fu_device_add_vendor_id(FU_DEVICE(self), "USB:0x413C");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INSTALL_SKIP_VERSION_CHECK);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SKIPS_RESTART);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_EXPLICIT_ORDER);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
}

static void
fu_dell_kestrel_rmm_class_init(FuDellKestrelRmmClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_dell_kestrel_rmm_write;
	device_class->setup = fu_dell_kestrel_rmm_setup;
	device_class->set_progress = fu_dell_kestrel_rmm_set_progress;
	device_class->convert_version = fu_dell_kestrel_rmm_convert_version;
}

FuDellKestrelRmm *
fu_dell_kestrel_rmm_new(FuUsbDevice *device)
{
	FuDellKestrelRmm *self = g_object_new(FU_TYPE_DELL_KESTREL_RMM, NULL);

	fu_device_incorporate(FU_DEVICE(self), FU_DEVICE(device));
	return self;
}

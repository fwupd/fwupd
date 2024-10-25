/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include <string.h>

#include "fu-dell-k2-common.h"

struct _FuDellK2Rmm {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuDellK2Rmm, fu_dell_k2_rmm, FU_TYPE_HID_DEVICE)

static gchar *
fu_dell_k2_rmm_convert_version(FuDevice *device, guint64 version_raw)
{
	return g_strdup_printf("%u.%u.%u",
			       (guint)(version_raw >> 16) & 0xFF,
			       (guint)(version_raw >> 24) & 0xFF,
			       (guint)(version_raw >> 8) & 0xFF);
}

void
fu_dell_k2_rmm_fix_version(FuDevice *device)
{
	FuDevice *parent = NULL;

	/* use version given by parent */
	parent = fu_device_get_parent(device);
	if (parent != NULL) {
		guint32 rmm_version;

		rmm_version = fu_dell_k2_ec_get_rmm_version(parent);
		fu_device_set_version_raw(device, rmm_version);
	}
}

static gboolean
fu_dell_k2_rmm_setup(FuDevice *device, GError **error)
{
	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_dell_k2_rmm_parent_class)->setup(device, error))
		return FALSE;

	fu_dell_k2_rmm_fix_version(device);
	return TRUE;
}

static gboolean
fu_dell_k2_rmm_write(FuDevice *device,
		     FuFirmware *firmware,
		     FuProgress *progress,
		     FwupdInstallFlags flags,
		     GError **error)
{
	return fu_dell_k2_ec_write_firmware_helper(device,
						   firmware,
						   DELL_K2_EC_DEV_TYPE_RMM,
						   0,
						   error);
}

static void
fu_dell_k2_rmm_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 13, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 72, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 9, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 7, "reload");
}

static void
fu_dell_k2_rmm_init(FuDellK2Rmm *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.dell.k2");
	fu_device_add_vendor_id(FU_DEVICE(self), "USB:0x413C");
	fu_device_add_icon(FU_DEVICE(self), "dock-usb");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INSTALL_SKIP_VERSION_CHECK);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_EXPLICIT_ORDER);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
}

static void
fu_dell_k2_rmm_class_init(FuDellK2RmmClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_dell_k2_rmm_write;
	device_class->setup = fu_dell_k2_rmm_setup;
	device_class->set_progress = fu_dell_k2_rmm_set_progress;
	device_class->convert_version = fu_dell_k2_rmm_convert_version;
}

FuDellK2Rmm *
fu_dell_k2_rmm_new(FuUsbDevice *device)
{
	FuDellK2Rmm *self = g_object_new(FU_TYPE_DELL_K2_RMM, NULL);

	fu_device_incorporate(FU_DEVICE(self), FU_DEVICE(device), FU_DEVICE_INCORPORATE_FLAG_ALL);
	return self;
}

/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include <string.h>

#include "fu-dell-dock2-common.h"

struct _FuDellDock2Rmm {
	FuDevice parent_instance;
	FuDellDock2BaseType dock_type;
};

G_DEFINE_TYPE(FuDellDock2Rmm, fu_dell_dock2_rmm, FU_TYPE_USB_DEVICE)

static gchar *
fu_dell_dock2_rmm_convert_version(FuDevice *device, guint64 version_raw)
{
	return g_strdup_printf("%u.%u.%u",
			       (guint)(version_raw >> 16) & 0xFF,
			       (guint)(version_raw >> 24) & 0xFF,
			       (guint)(version_raw >> 8) & 0xFF);
}

static gboolean
fu_dell_dock2_rmm_setup(FuDevice *device, GError **error)
{
	FuDellDock2Rmm *self = FU_DELL_DOCK2_RMM(device);
	g_autofree const gchar *devname = NULL;
	guint8 dev_type = DELL_DOCK2_EC_DEV_TYPE_RMM;

	/* name */
	devname = g_strdup_printf("%s", fu_dell_dock2_ec_devicetype_to_str(dev_type, 0, 0));
	fu_device_set_name(device, devname);
	fu_device_set_logical_id(device, devname);

	/* IDs */
	fu_device_add_instance_u8(device, "DOCKTYPE", self->dock_type);
	fu_device_add_instance_u8(device, "DEVTYPE", dev_type);
	fu_device_build_instance_id(device,
				    error,
				    "USB",
				    "VID",
				    "PID",
				    "DOCKTYPE",
				    "DEVTYPE",
				    NULL);
	return TRUE;
}

void
fu_dell_dock2_rmm_setup_version_raw(FuDevice *device, const guint32 version_raw)
{
	fu_device_set_version_raw(device, version_raw);
}

static gboolean
fu_dell_dock2_rmm_write(FuDevice *device,
			FuFirmware *firmware,
			FuProgress *progress,
			FwupdInstallFlags flags,
			GError **error)
{
	return TRUE;
}

static void
fu_dell_dock2_rmm_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 13, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 72, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 9, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 7, "reload");
}

static void
fu_dell_dock2_rmm_init(FuDellDock2Rmm *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.dell.dock2");
	fu_device_add_vendor_id(FU_DEVICE(self), "USB:0x413C");
	fu_device_add_icon(FU_DEVICE(self), "dock-usb");
	fu_device_set_name(FU_DEVICE(self), "Remote Management");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_TRIPLET);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_EXPLICIT_ORDER);
}

static void
fu_dell_dock2_rmm_class_init(FuDellDock2RmmClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_dell_dock2_rmm_write;
	device_class->setup = fu_dell_dock2_rmm_setup;
	device_class->set_progress = fu_dell_dock2_rmm_set_progress;
	device_class->convert_version = fu_dell_dock2_rmm_convert_version;
}

FuDellDock2Rmm *
fu_dell_dock2_rmm_new(FuUsbDevice *device, FuDellDock2BaseType dock_type)
{
	FuDellDock2Rmm *self = g_object_new(FU_TYPE_DELL_DOCK2_RMM, NULL);

	fu_device_incorporate(FU_DEVICE(self), FU_DEVICE(device));
	self->dock_type = dock_type;
	return self;
}

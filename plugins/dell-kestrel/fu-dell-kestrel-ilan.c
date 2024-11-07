/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include <string.h>

#include "fu-dell-kestrel-common.h"

struct _FuDellKestrelIlan {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuDellKestrelIlan, fu_dell_kestrel_ilan, FU_TYPE_DEVICE)

static gchar *
fu_dell_kestrel_ilan_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint16_hex(version_raw, fu_device_get_version_format(device));
}

static gboolean
fu_dell_kestrel_ilan_setup(FuDevice *device, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	FuDellDockBaseType dock_type = fu_dell_kestrel_ec_get_dock_type(proxy);
	FuDellKestrelEcDevType dev_type = FU_DELL_KESTREL_EC_DEV_TYPE_LAN;
	guint32 version_raw;
	g_autofree gchar *devname = NULL;

	/* name */
	devname = g_strdup_printf("%s", fu_dell_kestrel_ec_devicetype_to_str(dev_type, 0, 0));
	fu_device_set_name(device, devname);
	fu_device_set_logical_id(device, devname);

	/* instance ID */
	fu_device_add_instance_u8(device, "DOCKTYPE", dock_type);
	fu_device_add_instance_u8(device, "DEVTYPE", dev_type);
	fu_device_build_instance_id(device, error, "EC", "DOCKTYPE", "DEVTYPE", NULL);

	/* version */
	version_raw = fu_dell_kestrel_ec_get_ilan_version(proxy);
	fu_device_set_version_raw(device, version_raw);
	return TRUE;
}

static gboolean
fu_dell_kestrel_ilan_write(FuDevice *device,
			   FuFirmware *firmware,
			   FuProgress *progress,
			   FwupdInstallFlags flags,
			   GError **error)
{
	return fu_dell_kestrel_ec_write_firmware_helper(fu_device_get_proxy(device),
							firmware,
							progress,
							FU_DELL_KESTREL_EC_DEV_TYPE_LAN,
							0,
							error);
}

static void
fu_dell_kestrel_ilan_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 13, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 72, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 9, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 7, "reload");
}

static void
fu_dell_kestrel_ilan_init(FuDellKestrelIlan *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.dell.kestrel");
	fu_device_add_vendor_id(FU_DEVICE(self), "USB:0x413C");
	fu_device_add_icon(FU_DEVICE(self), "network-wired");
	fu_device_set_summary(FU_DEVICE(self), "Dell Dock LAN");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INSTALL_SKIP_VERSION_CHECK);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_EXPLICIT_ORDER);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PAIR);
}

static void
fu_dell_kestrel_ilan_class_init(FuDellKestrelIlanClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_dell_kestrel_ilan_write;
	device_class->setup = fu_dell_kestrel_ilan_setup;
	device_class->set_progress = fu_dell_kestrel_ilan_set_progress;
	device_class->convert_version = fu_dell_kestrel_ilan_convert_version;
}

FuDellKestrelIlan *
fu_dell_kestrel_ilan_new(FuDevice *proxy)
{
	FuContext *ctx = fu_device_get_context(proxy);
	FuDellKestrelIlan *self = NULL;
	self = g_object_new(FU_TYPE_DELL_KESTREL_ILAN, "context", ctx, NULL);
	fu_device_set_proxy(FU_DEVICE(self), proxy);
	return self;
}

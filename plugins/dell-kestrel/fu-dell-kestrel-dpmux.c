/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include <string.h>

#include "fu-dell-kestrel-common.h"

struct _FuDellKestrelDpmux {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuDellKestrelDpmux, fu_dell_kestrel_dpmux, FU_TYPE_DEVICE)

static gchar *
fu_dell_kestrel_dpmux_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32_hex(version_raw, fu_device_get_version_format(device));
}

static gboolean
fu_dell_kestrel_dpmux_setup(FuDevice *device, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	FuDellDockBaseType dock_type = fu_dell_kestrel_ec_get_dock_type(proxy);
	FuDellKestrelDockSku dock_sku = fu_dell_kestrel_ec_get_dock_sku(proxy);
	FuDellKestrelEcDevType dev_type = FU_DELL_KESTREL_EC_DEV_TYPE_DP_MUX;
	guint32 dpmux_version;
	g_autofree gchar *devname = NULL;

	/* name */
	devname = g_strdup_printf("%s", fu_dell_kestrel_ec_devicetype_to_str(dev_type, 0, 0));
	fu_device_set_name(device, devname);
	fu_device_set_logical_id(device, devname);

	/* instance ID */
	fu_device_add_instance_u8(device, "DOCKTYPE", dock_type);
	fu_device_add_instance_u8(device, "DOCKSKU", dock_sku);
	fu_device_add_instance_u8(device, "DEVTYPE", dev_type);
	fu_device_build_instance_id(device, error, "EC", "DOCKTYPE", "DOCKSKU", "DEVTYPE", NULL);

	/* version */
	dpmux_version = fu_dell_kestrel_ec_get_dpmux_version(proxy);
	fu_device_set_version_raw(device, dpmux_version);

	return TRUE;
}

static gboolean
fu_dell_kestrel_dpmux_write(FuDevice *device,
			    FuFirmware *firmware,
			    FuProgress *progress,
			    FwupdInstallFlags flags,
			    GError **error)
{
	return fu_dell_kestrel_ec_write_firmware_helper(fu_device_get_proxy(device),
							firmware,
							progress,
							FU_DELL_KESTREL_EC_DEV_TYPE_DP_MUX,
							0,
							error);
}

static void
fu_dell_kestrel_dpmux_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_dell_kestrel_dpmux_init(FuDellKestrelDpmux *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.dell.kestrel");
	fu_device_add_vendor_id(FU_DEVICE(self), "USB:0x413C");
	fu_device_add_icon(FU_DEVICE(self), "thunderbolt");
	fu_device_set_summary(FU_DEVICE(self), "Dell Dock Retimer");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INSTALL_SKIP_VERSION_CHECK);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_EXPLICIT_ORDER);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
}

static void
fu_dell_kestrel_dpmux_class_init(FuDellKestrelDpmuxClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_dell_kestrel_dpmux_write;
	device_class->setup = fu_dell_kestrel_dpmux_setup;
	device_class->set_progress = fu_dell_kestrel_dpmux_set_progress;
	device_class->convert_version = fu_dell_kestrel_dpmux_convert_version;
}

FuDellKestrelDpmux *
fu_dell_kestrel_dpmux_new(FuDevice *proxy)
{
	FuContext *ctx = fu_device_get_context(proxy);
	FuDellKestrelDpmux *self = NULL;
	self = g_object_new(FU_TYPE_DELL_KESTREL_DPMUX, "context", ctx, NULL);
	fu_device_set_proxy(FU_DEVICE(self), proxy);
	return self;
}

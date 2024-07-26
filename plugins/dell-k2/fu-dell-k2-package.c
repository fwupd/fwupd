/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include <string.h>

#include "fu-dell-k2-common.h"
#include "fu-dell-k2-ec-hid.h"
#include "fu-dell-k2-ec-struct.h"
#include "fu-dell-k2-package.h"

#define FU_DELL_K2_PACKAGE_VERSION_OFFSET 0x14

struct _FuDellK2Package {
	FuDevice parent_instance;
	guint64 blob_version_offset;
};

G_DEFINE_TYPE(FuDellK2Package, fu_dell_k2_package, FU_TYPE_DEVICE)

static gchar *
fu_dell_k2_package_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32_hex(version_raw, fu_device_get_version_format(device));
}

static gboolean
fu_dell_k2_package_setup(FuDevice *device, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	guint32 pkg_version_raw;
	guint8 dock_sku = 0;
	FuDellK2BaseType dock_type;

	/* instance ID */
	dock_type = fu_dell_k2_ec_get_dock_type(proxy);
	dock_sku = fu_dell_k2_ec_get_dock_sku(proxy);

	fu_device_add_instance_u8(device, "DOCKTYPE", dock_type);
	fu_device_add_instance_u8(device, "DOCKSKU", dock_sku);
	fu_device_add_instance_strup(device, "DEVTYPE", "PACKAGE");
	fu_device_build_instance_id(device, error, "EC", "DOCKTYPE", "DOCKSKU", "DEVTYPE", NULL);

	/* setup version */
	pkg_version_raw = fu_dell_k2_ec_get_package_version(proxy);
	fu_device_set_version_raw(device, GUINT32_FROM_BE(pkg_version_raw));

	return TRUE;
}

static gboolean
fu_dell_k2_package_write(FuDevice *device,
			 FuFirmware *firmware,
			 FuProgress *progress,
			 FwupdInstallFlags flags,
			 GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	gsize length = 0;
	guint32 status_version = 0;
	const guint8 *data;
	g_autofree gchar *dynamic_version = NULL;
	g_autoptr(GBytes) fw = NULL;

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);

	/* get default image */
	fw = fu_firmware_get_bytes(firmware, error);
	if (fw == NULL)
		return FALSE;
	data = g_bytes_get_data(fw, &length);
	if (!fu_memcpy_safe((guint8 *)&status_version,
			    sizeof(status_version),
			    0x0,
			    data,
			    length,
			    FU_DELL_K2_PACKAGE_VERSION_OFFSET,
			    sizeof(status_version),
			    error))
		return FALSE;
	dynamic_version =
	    fu_version_from_uint32_hex(status_version, fu_device_get_version_format(device));
	g_info("writing package status version %s", dynamic_version);

	if (!fu_dell_k2_ec_commit_package(proxy, fw, error))
		return FALSE;

	/* dock will reboot to re-read; this is to appease the daemon */
	fu_device_set_version(device, dynamic_version); /* nocheck:set-version */

	return TRUE;
}

static void
fu_dell_k2_package_finalize(GObject *object)
{
	G_OBJECT_CLASS(fu_dell_k2_package_parent_class)->finalize(object);
}

static void
fu_dell_k2_package_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 45, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 55, "reload");
}

static void
fu_dell_k2_package_init(FuDellK2Package *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.dell.k2");
	fu_device_add_vendor_id(FU_DEVICE(self), "USB:0x413C");
	fu_device_set_name(FU_DEVICE(self), "Package level of Dell dock");
	fu_device_set_summary(FU_DEVICE(self), "A representation of dock update status");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_EXPLICIT_ORDER);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_USE_PROXY_FOR_OPEN);
}

static void
fu_dell_k2_package_class_init(FuDellK2PackageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_dell_k2_package_finalize;
	device_class->write_firmware = fu_dell_k2_package_write;
	device_class->setup = fu_dell_k2_package_setup;
	device_class->set_progress = fu_dell_k2_package_set_progress;
	device_class->convert_version = fu_dell_k2_package_convert_version;
}

FuDellK2Package *
fu_dell_k2_package_new(FuDevice *proxy)
{
	FuContext *ctx = fu_device_get_context(proxy);
	FuDellK2Package *self = NULL;
	self = g_object_new(FU_TYPE_DELL_K2_PACKAGE, "context", ctx, NULL);
	fu_device_set_proxy(FU_DEVICE(self), proxy);
	fu_device_set_logical_id(FU_DEVICE(self), "package");
	return self;
}

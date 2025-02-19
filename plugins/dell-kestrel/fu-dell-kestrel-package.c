/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include "fu-dell-kestrel-common.h"
#include "fu-dell-kestrel-ec-struct.h"
#include "fu-dell-kestrel-package.h"

struct _FuDellKestrelPackage {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuDellKestrelPackage, fu_dell_kestrel_package, FU_TYPE_DEVICE)

static gchar *
fu_dell_kestrel_package_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32_hex(version_raw, fu_device_get_version_format(device));
}

static gboolean
fu_dell_kestrel_package_setup(FuDevice *device, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	FuDellKestrelDockSku dock_sku = fu_dell_kestrel_ec_get_dock_sku(FU_DELL_KESTREL_EC(proxy));
	FuDellDockBaseType dock_type = fu_dell_kestrel_ec_get_dock_type(FU_DELL_KESTREL_EC(proxy));
	guint32 pkg_version_raw;

	/* instance ID */
	fu_device_add_instance_u8(device, "DOCKTYPE", dock_type);
	fu_device_add_instance_u8(device, "DOCKSKU", dock_sku);
	fu_device_add_instance_strup(device, "DEVTYPE", "PACKAGE");
	fu_device_build_instance_id(device, error, "EC", "DOCKTYPE", "DOCKSKU", "DEVTYPE", NULL);

	/* setup version */
	pkg_version_raw = fu_dell_kestrel_ec_get_package_version(FU_DELL_KESTREL_EC(proxy));
	fu_device_set_version_raw(device, pkg_version_raw);

	return TRUE;
}

static gboolean
fu_dell_kestrel_package_write(FuDevice *device,
			      FuFirmware *firmware,
			      FuProgress *progress,
			      FwupdInstallFlags flags,
			      GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);
	guint32 pkg_version = 0;
	g_autoptr(GInputStream) stream = NULL;
	g_autofree gchar *dynamic_version = NULL;

	g_return_val_if_fail(device != NULL, FALSE);
	g_return_val_if_fail(FU_IS_FIRMWARE(firmware), FALSE);

	/* get default image */
	stream = fu_firmware_get_stream(firmware, error);
	if (stream == NULL)
		return FALSE;

	/* get the new package version */
	if (!fu_input_stream_read_u32(stream, 0, &pkg_version, G_BIG_ENDIAN, error))
		return FALSE;

	/* print the package version */
	dynamic_version =
	    fu_version_from_uint32_hex(pkg_version, fu_device_get_version_format(device));
	g_debug("writing firmware: %s, %s -> %s",
		fu_device_get_name(device),
		fu_device_get_version(device),
		dynamic_version);

	/* write to device */
	if (!fu_dell_kestrel_ec_commit_package(FU_DELL_KESTREL_EC(proxy), stream, error))
		return FALSE;

	/* dock will reboot to re-read; this is to appease the daemon */
	fu_device_set_version(device, dynamic_version); /* nocheck:set-version */

	return TRUE;
}

static gboolean
fu_dell_kestrel_package_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	FuDevice *proxy = fu_device_get_proxy(device);

	/* register post message */
	if (fu_device_has_flag(proxy, FWUPD_DEVICE_FLAG_USABLE_DURING_UPDATE)) {
		g_autoptr(FwupdRequest) request = fwupd_request_new();

		fwupd_request_set_kind(request, FWUPD_REQUEST_KIND_POST);
		fwupd_request_set_id(request, FWUPD_REQUEST_ID_REMOVE_USB_CABLE);
		fwupd_request_add_flag(request, FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
		return fu_device_emit_request(device, request, progress, error);
	}
	return TRUE;
}

static void
fu_dell_kestrel_package_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DECOMPRESSING, 0, "prepare-fw");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 45, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 55, "reload");
}

static void
fu_dell_kestrel_package_init(FuDellKestrelPackage *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.dell.kestrel");
	fu_device_add_vendor_id(FU_DEVICE(self), "USB:0x413C");
	fu_device_set_name(FU_DEVICE(self), "Package Version of Dell dock");
	fu_device_set_summary(FU_DEVICE(self), "Dell Dock Package");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_request_flag(FU_DEVICE(self), FWUPD_REQUEST_FLAG_ALLOW_GENERIC_MESSAGE);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_EXPLICIT_ORDER);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FOR_OPEN);
}

static void
fu_dell_kestrel_package_class_init(FuDellKestrelPackageClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_dell_kestrel_package_write;
	device_class->setup = fu_dell_kestrel_package_setup;
	device_class->set_progress = fu_dell_kestrel_package_set_progress;
	device_class->convert_version = fu_dell_kestrel_package_convert_version;
	device_class->attach = fu_dell_kestrel_package_attach;
}

FuDellKestrelPackage *
fu_dell_kestrel_package_new(FuDevice *proxy)
{
	FuContext *ctx = fu_device_get_context(proxy);
	FuDellKestrelPackage *self = NULL;
	self = g_object_new(FU_TYPE_DELL_KESTREL_PACKAGE, "context", ctx, NULL);
	fu_device_set_proxy(FU_DEVICE(self), proxy);
	fu_device_set_logical_id(FU_DEVICE(self), "package");
	return self;
}

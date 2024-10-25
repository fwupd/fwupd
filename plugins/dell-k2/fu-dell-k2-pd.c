/*
 * Copyright 2024 Dell Technologies
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later OR MIT
 */

#include "config.h"

#include <string.h>

#include "fu-dell-k2-common.h"
#include "fu-dell-k2-ec-struct.h"
#include "fu-dell-k2-pd-firmware.h"

struct _FuDellK2Pd {
	FuDevice parent_instance;
	guint64 blob_version_offset;
	guint8 pd_subtype;
	guint8 pd_instance;
	guint8 pd_identifier;
};

G_DEFINE_TYPE(FuDellK2Pd, fu_dell_k2_pd, FU_TYPE_DEVICE)

static gchar *
fu_dell_k2_pd_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32_hex(version_raw, fu_device_get_version_format(device));
}

static gboolean
fu_dell_k2_pd_setup(FuDevice *device, GError **error)
{
	FuDellK2Pd *self = FU_DELL_K2_PD(device);
	FuDevice *proxy = fu_device_get_proxy(device);
	FuDellK2BaseType dock_type;
	guint8 dock_sku;
	guint32 raw_version;
	guint8 dev_type = FU_DELL_K2_EC_DEV_TYPE_PD;
	g_autofree gchar *devname = NULL;

	/* name */
	devname = g_strdup(
	    fu_dell_k2_ec_devicetype_to_str(dev_type, self->pd_subtype, self->pd_instance));
	fu_device_set_name(device, devname);
	fu_device_set_logical_id(device, devname);

	/* instance ID */
	dock_type = fu_dell_k2_ec_get_dock_type(proxy);
	dock_sku = fu_dell_k2_ec_get_dock_sku(proxy);

	fu_device_add_instance_u8(device, "DOCKTYPE", dock_type);
	fu_device_add_instance_u8(device, "DOCKSKU", dock_sku);
	fu_device_add_instance_u8(device, "DEVTYPE", dev_type);
	fu_device_add_instance_u8(device, "INST", self->pd_instance);
	fu_device_build_instance_id(device,
				    error,
				    "EC",
				    "DOCKTYPE",
				    "DOCKSKU",
				    "DEVTYPE",
				    "INST",
				    NULL);

	/* version */
	raw_version = fu_dell_k2_ec_get_pd_version(proxy, self->pd_subtype, self->pd_instance);
	fu_device_set_version_raw(device, raw_version);

	return TRUE;
}

static gboolean
fu_dell_k2_pd_write(FuDevice *device,
		    FuFirmware *firmware,
		    FuProgress *progress,
		    FwupdInstallFlags flags,
		    GError **error)
{
	FuDellK2Pd *self = FU_DELL_K2_PD(device);

	return fu_dell_k2_ec_write_firmware_helper(fu_device_get_proxy(device),
						   firmware,
						   FU_DELL_K2_EC_DEV_TYPE_PD,
						   self->pd_identifier,
						   error);
}

static void
fu_dell_k2_pd_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 100, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 0, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 0, "reload");
}

static void
fu_dell_k2_pd_init(FuDellK2Pd *self)
{
	fu_device_add_protocol(FU_DEVICE(self), "com.dell.k2");
	fu_device_add_vendor_id(FU_DEVICE(self), "USB:0x413C");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INSTALL_SKIP_VERSION_CHECK);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_SKIPS_RESTART);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_EXPLICIT_ORDER);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_USE_PROXY_FOR_OPEN);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_QUAD);
	fu_device_set_firmware_gtype(FU_DEVICE(self), FU_TYPE_DELL_K2_PD_FIRMWARE);
}

static void
fu_dell_k2_pd_class_init(FuDellK2PdClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->write_firmware = fu_dell_k2_pd_write;
	device_class->setup = fu_dell_k2_pd_setup;
	device_class->set_progress = fu_dell_k2_pd_set_progress;
	device_class->convert_version = fu_dell_k2_pd_convert_version;
}

FuDellK2Pd *
fu_dell_k2_pd_new(FuDevice *proxy, guint8 subtype, guint8 instance)
{
	FuContext *ctx = fu_device_get_context(proxy);
	FuDellK2Pd *self = NULL;
	self = g_object_new(FU_TYPE_DELL_K2_PD, "context", ctx, NULL);
	self->pd_subtype = subtype;
	self->pd_instance = instance;
	self->pd_identifier = instance + 1;
	fu_device_set_proxy(FU_DEVICE(self), proxy);
	return self;
}

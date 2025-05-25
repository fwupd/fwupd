/*
 * Copyright 2025 Mario Limonciello <superm1@gmail.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-legion-hid2-device.h"
#include "fu-legion-hid2-sipo-device.h"
#include "fu-legion-hid2-struct.h"

struct _FuLegionHid2SipoDevice {
	FuDevice parent_instance;
};

G_DEFINE_TYPE(FuLegionHid2SipoDevice, fu_legion_hid2_sipo_device, FU_TYPE_DEVICE)

static gboolean
fu_legion_hid2_sipo_device_probe(FuDevice *device, GError **error)
{
	return fu_device_build_instance_id(device, NULL, "USB", "VID", "PID", "TP", NULL);
}

static gboolean
fu_legion_hid2_sipo_device_write_firmware(FuDevice *device,
					  FuFirmware *firmware,
					  FuProgress *progress,
					  FwupdInstallFlags flags,
					  GError **error)
{
	FuLegionHid2SipoDevice *self = FU_LEGION_HID2_SIPO_DEVICE(device);
	FuLegionHid2Device *proxy = FU_LEGION_HID2_DEVICE(fu_device_get_proxy(FU_DEVICE(self)));

	if (proxy == NULL) {
		g_set_error_literal(error, FWUPD_ERROR, FWUPD_ERROR_NOT_SUPPORTED, "no proxy");
		return FALSE;
	}

	g_set_error_literal(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "not yet implemented for SIPO touchpads");
	return FALSE;
}

static gchar *
fu_legion_hid2_sipo_device_convert_version(FuDevice *device, guint64 version_raw)
{
	return fu_version_from_uint32(version_raw, fu_device_get_version_format(device));
}

static void
fu_legion_hid2_sipo_device_init(FuLegionHid2SipoDevice *self)
{
	fu_device_set_name(FU_DEVICE(self), "Touchpad");
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_USE_PROXY_FALLBACK);
	fu_device_add_private_flag(FU_DEVICE(self), FU_DEVICE_PRIVATE_FLAG_REFCOUNTED_PROXY);
	fu_device_add_protocol(FU_DEVICE(self), "com.lenovo.legion-hid2");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_INTERNAL);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_NUMBER);
	fu_device_set_logical_id(FU_DEVICE(self), "touchpad");
	fu_device_set_vendor(FU_DEVICE(self), "SIPO");
	fu_device_add_instance_strsafe(FU_DEVICE(self), "TP", "SIPO");
}

static void
fu_legion_hid2_sipo_device_class_init(FuLegionHid2SipoDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_legion_hid2_sipo_device_probe;
	device_class->write_firmware = fu_legion_hid2_sipo_device_write_firmware;
	device_class->convert_version = fu_legion_hid2_sipo_device_convert_version;
}

FuDevice *
fu_legion_hid2_sipo_device_new(FuDevice *proxy)
{
	return g_object_new(FU_TYPE_LEGION_HID2_SIPO_DEVICE, "proxy", proxy, NULL);
}

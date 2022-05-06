/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-scsi-device.h"

struct _FuScsiDevice {
	FuUdevDevice parent_instance;
};

G_DEFINE_TYPE(FuScsiDevice, fu_scsi_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_scsi_device_probe(FuDevice *device, GError **error)
{
	GUdevDevice *udev_device = fu_udev_device_get_dev(FU_UDEV_DEVICE(device));
	guint64 removable = 0;
	g_autofree gchar *vendor_id = NULL;

	/* check is valid */
	if (g_strcmp0(g_udev_device_get_devtype(udev_device), "disk") != 0) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "is not correct devtype=%s, expected disk",
			    g_udev_device_get_devtype(udev_device));
		return FALSE;
	}
	if (!g_udev_device_get_property_as_boolean(udev_device, "ID_SCSI")) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "has no ID_SCSI");
		return FALSE;
	}

	/* vendor sanity */
	if (g_strcmp0(fu_device_get_vendor(device), "ATA") == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no assigned vendor");
		return FALSE;
	}
	vendor_id = g_strdup_printf("SCSI:%s", fu_device_get_vendor(device));
	fu_device_add_vendor_id(device, vendor_id);

	/* add GUIDs */
	fu_device_add_instance_strsafe(device, "VEN", fu_device_get_vendor(device));
	fu_device_add_instance_strsafe(device, "DEV", fu_device_get_name(device));
	fu_device_add_instance_strsafe(device, "REV", fu_device_get_version(device));
	if (!fu_device_build_instance_id_quirk(device, error, "SCSI", "VEN", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "SCSI", "VEN", "DEV", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "SCSI", "VEN", "DEV", "REV", NULL))
		return FALSE;

	/* is internal? */
	if (fu_udev_device_get_sysfs_attr_uint64(FU_UDEV_DEVICE(device),
						 "removable",
						 &removable,
						 NULL)) {
		if (removable == 0x0)
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
	}

	/* set the physical ID */
	return fu_udev_device_set_physical_id(FU_UDEV_DEVICE(device), "scsi", error);
}

static void
fu_scsi_device_init(FuScsiDevice *self)
{
	fu_device_add_icon(FU_DEVICE(self), "drive-harddisk");
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_PLAIN);
	fu_device_set_summary(FU_DEVICE(self), "SCSI device");
}

static void
fu_scsi_device_finalize(GObject *object)
{
	G_OBJECT_CLASS(fu_scsi_device_parent_class)->finalize(object);
}

static void
fu_scsi_device_class_init(FuScsiDeviceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	object_class->finalize = fu_scsi_device_finalize;
	klass_device->probe = fu_scsi_device_probe;
}

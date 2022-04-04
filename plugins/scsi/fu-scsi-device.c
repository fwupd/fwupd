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
	const gchar *name;
	const gchar *vendor;
	const gchar *version;
	g_autofree gchar *vendor_id = NULL;
	g_autofree gchar *vendor_safe = NULL;
	g_autoptr(GPtrArray) block_devs = NULL;

	/* ignore */
	if (g_strcmp0(fu_udev_device_get_devtype(FU_UDEV_DEVICE(device)), "scsi_target") == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "targets are not supported");
		return FALSE;
	}

	/* vendor */
	vendor = fu_udev_device_get_sysfs_attr(FU_UDEV_DEVICE(device), "vendor", NULL);
	if (vendor != NULL)
		vendor_safe = fu_common_strstrip(vendor);
	if (vendor_safe == NULL || g_strcmp0(vendor_safe, "ATA") == 0) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_NOT_SUPPORTED,
				    "no assigned vendor");
		return FALSE;
	}
	fu_device_set_vendor(device, vendor);
	vendor_id = g_strdup_printf("SCSI:%s", vendor_safe);
	fu_device_add_vendor_id(device, vendor_id);

	/* name */
	name = fu_udev_device_get_sysfs_attr(FU_UDEV_DEVICE(device), "model", NULL);
	fu_device_set_name(device, name);

	/* version */
	version = fu_udev_device_get_sysfs_attr(FU_UDEV_DEVICE(device), "rev", NULL);
	fu_device_set_version(device, version);

	/* add GUIDs */
	fu_device_add_instance_strsafe(device, "VEN", vendor);
	fu_device_add_instance_strsafe(device, "DEV", name);
	fu_device_add_instance_strsafe(device, "REV", version);
	if (!fu_device_build_instance_id_quirk(device, error, "SCSI", "VEN", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "SCSI", "VEN", "DEV", NULL))
		return FALSE;
	if (!fu_device_build_instance_id(device, error, "SCSI", "VEN", "DEV", "REV", NULL))
		return FALSE;
	/* check all block devices, although there should only be one */
	block_devs = fu_udev_device_get_children_with_subsystem(FU_UDEV_DEVICE(device), "block");
	for (guint i = 0; i < block_devs->len; i++) {
		FuUdevDevice *block_dev = g_ptr_array_index(block_devs, i);
		guint64 value = 0;
		if (!fu_udev_device_get_sysfs_attr_uint64(block_dev, "removable", &value, NULL))
			continue;
		if (value == 0x0) {
			fu_device_add_flag(device, FWUPD_DEVICE_FLAG_INTERNAL);
			break;
		}
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

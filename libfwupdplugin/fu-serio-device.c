/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuSerioDevice"

#include "config.h"

#include "fu-serio-device.h"
#include "fu-udev-device-private.h"

/**
 * FuSerioDevice
 *
 * See also: #FuUdevDevice
 */

G_DEFINE_TYPE(FuSerioDevice, fu_serio_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_serio_device_probe(FuDevice *device, GError **error)
{
	FuSerioDevice *self = FU_SERIO_DEVICE(device);
	g_auto(GStrv) sysfs_parts = NULL;
	g_autofree gchar *summary = NULL;
	g_autofree gchar *firmware_id = NULL;

	/* FuUdevDevice */
	if (!FU_DEVICE_CLASS(fu_serio_device_parent_class)->probe(device, error))
		return FALSE;

	/* firmware ID */
	firmware_id = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
						"firmware_id",
						FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
						NULL);
	if (firmware_id != NULL && firmware_id[0] != '\0') {
		g_autofree gchar *firmware_id_strup = g_ascii_strup(firmware_id, -1);
		if (g_str_has_prefix(firmware_id, "PNP: "))
			fu_device_add_instance_strsafe(device, "FWID", firmware_id_strup + 5);
		else
			fu_device_add_instance_strsafe(device, "FWID", firmware_id_strup);
		if (!fu_device_build_instance_id_full(device,
						      FU_DEVICE_INSTANCE_FLAG_GENERIC |
							  FU_DEVICE_INSTANCE_FLAG_VISIBLE |
							  FU_DEVICE_INSTANCE_FLAG_QUIRKS,
						      error,
						      "SERIO",
						      "FWID",
						      NULL))
			return FALSE;
	}

	/* try to get one line summary */
	summary = fu_udev_device_read_sysfs(FU_UDEV_DEVICE(self),
					    "description",
					    FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					    NULL);
	if (summary != NULL)
		fu_device_set_summary(device, summary);

	/* fall back to the first thing handled by misc drivers */
	if (fu_udev_device_get_device_file(FU_UDEV_DEVICE(self)) == NULL) {
		g_autofree gchar *device_file =
		    fu_udev_device_get_device_file_from_subsystem(FU_UDEV_DEVICE(self),
								  "misc",
								  NULL);
		if (device_file != NULL)
			fu_udev_device_set_device_file(FU_UDEV_DEVICE(self), device_file);
	}

	/* we don't have anything better to use */
	sysfs_parts = g_strsplit(fu_udev_device_get_sysfs_path(FU_UDEV_DEVICE(self)), "/sys", 2);
	if (sysfs_parts[1] != NULL) {
		g_autofree gchar *physical_id = g_strdup_printf("DEVPATH=%s", sysfs_parts[1]);
		fu_device_set_physical_id(device, physical_id);
	}

	/* success */
	return TRUE;
}

static void
fu_serio_device_init(FuSerioDevice *self)
{
}

static void
fu_serio_device_class_init(FuSerioDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_serio_device_probe;
}

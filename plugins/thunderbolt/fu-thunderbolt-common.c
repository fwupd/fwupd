/*
 * Copyright 2017 Christian J. Kellner <christian@kellner.me>
 * Copyright 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-thunderbolt-common.h"

static gchar *
fu_thunderbolt_device_find_usb4_port_path(FuUdevDevice *device,
					  const gchar *attribute,
					  GError **error)
{
	const gchar *sysfs_path = fu_udev_device_get_sysfs_path(device);
	for (guint i = 0; i < 9; i++) {
		g_autofree gchar *path = g_strdup_printf("usb4_port%u/%s", i, attribute);
		g_autofree gchar *fn = g_build_filename(sysfs_path, path, NULL);
		g_autoptr(GFile) file = g_file_new_for_path(fn);
		if (g_file_query_exists(file, NULL))
			return g_steal_pointer(&path);
	}
	g_set_error(error,
		    FWUPD_ERROR,
		    FWUPD_ERROR_NOT_FOUND,
		    "failed to find usb4_port?/%s",
		    attribute);
	return NULL;
}

gboolean
fu_thunderbolt_udev_set_port_offline(FuUdevDevice *device, GError **error)
{
	g_autofree gchar *attribute = NULL;
	g_autoptr(GError) error_local = NULL;

	attribute = fu_thunderbolt_device_find_usb4_port_path(device, "offline", &error_local);
	if (attribute == NULL) {
		g_debug("failed to check usb4 offline path: %s", error_local->message);
		return TRUE;
	}
	if (!fu_udev_device_write_sysfs(device,
					attribute,
					"1",
					FU_THUNDERBOLT_DEVICE_WRITE_TIMEOUT,
					error)) {
		g_prefix_error(error, "setting usb4 port offline failed: ");
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_thunderbolt_udev_rescan_port(FuUdevDevice *device, GError **error)
{
	g_autofree gchar *attribute = NULL;
	g_autoptr(GError) error_local = NULL;

	attribute = fu_thunderbolt_device_find_usb4_port_path(device, "rescan", &error_local);
	if (attribute == NULL) {
		g_debug("failed to check usb4 rescan path: %s", error_local->message);
		return TRUE;
	}
	if (!fu_udev_device_write_sysfs(device,
					attribute,
					"1",
					FU_THUNDERBOLT_DEVICE_WRITE_TIMEOUT,
					error)) {
		g_prefix_error(error, "rescan on port failed: ");
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_thunderbolt_udev_set_port_online(FuUdevDevice *device, GError **error)
{
	FuUdevDevice *udev = FU_UDEV_DEVICE(device);
	g_autofree gchar *attribute = NULL;
	g_autoptr(GError) error_local = NULL;

	attribute = fu_thunderbolt_device_find_usb4_port_path(device, "offline", &error_local);
	if (attribute == NULL) {
		g_debug("failed to check usb4 port path: %s", error_local->message);
		return TRUE;
	}
	if (!fu_udev_device_write_sysfs(udev,
					attribute,
					"0",
					FU_THUNDERBOLT_DEVICE_WRITE_TIMEOUT,
					error)) {
		g_prefix_error(error, "setting port online failed: ");
		return FALSE;
	}
	return TRUE;
}

guint16
fu_thunderbolt_udev_get_attr_uint16(FuUdevDevice *device, const gchar *name, GError **error)
{
	guint64 val = 0;
	g_autofree gchar *str = NULL;

	str = fu_udev_device_read_sysfs(device,
					name,
					FU_UDEV_DEVICE_ATTR_READ_TIMEOUT_DEFAULT,
					error);
	if (str == NULL)
		return 0x0;
	if (!fu_strtoull(str, &val, 0, G_MAXUINT16, FU_INTEGER_BASE_16, error))
		return 0x0;
	return (guint16)val;
}

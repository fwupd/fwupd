/*
 * Copyright (C) 2017 Christian J. Kellner <christian@kellner.me>
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-thunderbolt-common.h"

static gboolean
fu_thunderbolt_device_check_usb4_port_path(FuUdevDevice *device,
					   const gchar *attribute,
					   GError **err)
{
	g_autofree const gchar *path =
	    g_build_filename(fu_udev_device_get_sysfs_path(device), attribute, NULL);
	g_autofree gchar *fn = g_strdup_printf("%s", path);
	g_autoptr(GFile) file = g_file_new_for_path(fn);
	if (!g_file_query_exists(file, NULL)) {
		g_set_error(err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "failed to find %s", fn);
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_thunderbolt_udev_set_port_offline(FuUdevDevice *device, GError **error)
{
	const gchar *offline = "usb4_port1/offline";
	const gchar *rescan = "usb4_port1/rescan";
	g_autoptr(GError) error_local = NULL;

	if (!fu_thunderbolt_device_check_usb4_port_path(device, offline, &error_local)) {
		g_debug("failed to check usb4 offline path: %s", error_local->message);
		return TRUE;
	}
	if (!fu_udev_device_write_sysfs(device, offline, "1", error)) {
		g_prefix_error(error, "setting usb4 port offline failed: ");
		return FALSE;
	}
	if (!fu_thunderbolt_device_check_usb4_port_path(device, rescan, &error_local)) {
		g_debug("failed to check usb4 rescan path: %s", error_local->message);
		return TRUE;
	}
	if (!fu_udev_device_write_sysfs(device, rescan, "1", error)) {
		g_prefix_error(error, "rescan on port failed: ");
		return FALSE;
	}
	return TRUE;
}

gboolean
fu_thunderbolt_udev_set_port_online(FuUdevDevice *device, GError **error)
{
	FuUdevDevice *udev = FU_UDEV_DEVICE(device);
	const gchar *offline = "usb4_port1/offline";
	g_autoptr(GError) error_local = NULL;

	if (!fu_thunderbolt_device_check_usb4_port_path(device, offline, &error_local)) {
		g_debug("failed to check usb4 port path: %s", error_local->message);
		return TRUE;
	}
	if (!fu_udev_device_write_sysfs(udev, offline, "0", error)) {
		g_prefix_error(error, "setting port online failed: ");
		return FALSE;
	}
	return TRUE;
}

guint16
fu_thunderbolt_udev_get_attr_uint16(FuUdevDevice *device, const gchar *name, GError **error)
{
	const gchar *str;
	guint64 val;

	str = fu_udev_device_get_sysfs_attr(device, name, error);
	if (str == NULL)
		return 0x0;

	val = g_ascii_strtoull(str, NULL, 16);
	if (val == 0x0) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "failed to parse %s", str);
		return 0;
	}
	if (val > G_MAXUINT16) {
		g_set_error(error, FWUPD_ERROR, FWUPD_ERROR_INTERNAL, "%s overflows", name);
		return 0x0;
	}
	return (guint16)val;
}

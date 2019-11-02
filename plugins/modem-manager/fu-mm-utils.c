/*
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <gio/gio.h>

#include "fu-udev-device.h"

#include "fu-mm-utils.h"

gboolean
fu_mm_utils_get_udev_port_info (GUdevDevice	 *device,
				gchar		**out_device_sysfs_path,
				gint		 *out_port_ifnum,
				GError		**error)
{
	gint port_ifnum = -1;
	const gchar *aux;
	g_autoptr(GUdevDevice) parent = NULL;
	g_autofree gchar *device_sysfs_path = NULL;

	/* ID_USB_INTERFACE_NUM is set on the port device itself */
	aux = g_udev_device_get_property (device, "ID_USB_INTERFACE_NUM");
	if (aux != NULL)
		port_ifnum = (guint16) g_ascii_strtoull (aux, NULL, 16);

	/* we need to traverse all parents of the give udev device until we find
	 * the first 'usb_device' reported, which is the GUdevDevice associated with
	 * the full USB device (i.e. all ports of the same device).
	 */
	parent = g_udev_device_get_parent (device);
	while (parent != NULL) {
		g_autoptr(GUdevDevice) next = NULL;

		if (g_strcmp0 (g_udev_device_get_devtype (parent), "usb_device") == 0) {
			device_sysfs_path = g_strdup (g_udev_device_get_sysfs_path (parent));
			break;
		}

		/* check next parent */
		next = g_udev_device_get_parent (parent);
		g_set_object (&parent, next);
	}

	if (parent == NULL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_FOUND,
			     "failed to lookup device info: parent usb_device not found");
		return FALSE;
	}

	if (out_port_ifnum != NULL)
		*out_port_ifnum = port_ifnum;
	if (out_device_sysfs_path != NULL)
		*out_device_sysfs_path = g_steal_pointer (&device_sysfs_path);
	return TRUE;
}

gboolean
fu_mm_utils_get_port_info (const gchar	 *path,
			   gchar	**out_device_sysfs_path,
			   gint		 *out_port_ifnum,
			   GError	**error)
{
	g_autoptr(GUdevClient) client = NULL;
	g_autoptr(GUdevDevice) dev = NULL;

	client = g_udev_client_new (NULL);
	dev = g_udev_client_query_by_device_file (client, path);
	if (dev == NULL) {
		g_set_error (error,
			     G_IO_ERROR,
			     G_IO_ERROR_NOT_FOUND,
			     "failed to lookup device by path");
		return FALSE;
	}

	return fu_mm_utils_get_udev_port_info (dev, out_device_sysfs_path, out_port_ifnum, error);
}

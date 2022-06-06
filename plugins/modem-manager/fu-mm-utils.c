/*
 * Copyright (C) 2019 Aleksander Morgado <aleksander@aleksander.es>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <fwupdplugin.h>

#include "fu-mm-utils.h"

static gchar *
find_device_bus_subsystem(GUdevDevice *device)
{
	g_autoptr(GUdevDevice) iter = NULL;

	iter = g_object_ref(device);
	while (iter != NULL) {
		g_autoptr(GUdevDevice) next = NULL;
		const gchar *subsys;

		/* stop search as soon as we find a parent object
		 * of one of the bus subsystems supported in
		 * ModemManager */
		subsys = g_udev_device_get_subsystem(iter);
		if ((g_strcmp0(subsys, "usb") == 0) || (g_strcmp0(subsys, "pcmcia") == 0) ||
		    (g_strcmp0(subsys, "pci") == 0) || (g_strcmp0(subsys, "platform") == 0) ||
		    (g_strcmp0(subsys, "pnp") == 0) || (g_strcmp0(subsys, "sdio") == 0)) {
			return g_ascii_strup(subsys, -1);
		}

		next = g_udev_device_get_parent(iter);
		g_set_object(&iter, next);
	}

	/* no more parents to check */
	return NULL;
}

gboolean
fu_mm_utils_get_udev_port_info(GUdevDevice *device,
			       gchar **out_device_bus,
			       gchar **out_device_sysfs_path,
			       gint *out_port_usb_ifnum,
			       GError **error)
{
	gint port_usb_ifnum = -1;
	g_autoptr(GUdevDevice) parent = NULL;
	g_autofree gchar *device_sysfs_path = NULL;
	g_autofree gchar *device_bus = NULL;

	/* lookup the main bus the device is in; for supported devices it will
	 * usually be either 'PCI' or 'USB' */
	device_bus = find_device_bus_subsystem(device);
	if (device_bus == NULL) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_FOUND,
			    "failed to lookup device info: bus not found");
		return FALSE;
	}

	if (g_strcmp0(device_bus, "USB") == 0) {
		/* ID_USB_INTERFACE_NUM is set on the port device itself */
		const gchar *aux = g_udev_device_get_property(device, "ID_USB_INTERFACE_NUM");
		if (aux != NULL)
			port_usb_ifnum = (guint16)g_ascii_strtoull(aux, NULL, 16);

		/* we need to traverse all parents of the give udev device until we find
		 * the first 'usb_device' reported, which is the GUdevDevice associated with
		 * the full USB device (i.e. all ports of the same device).
		 */
		parent = g_udev_device_get_parent(device);
		while (parent != NULL) {
			g_autoptr(GUdevDevice) next = NULL;

			if (g_strcmp0(g_udev_device_get_devtype(parent), "usb_device") == 0) {
				device_sysfs_path = g_strdup(g_udev_device_get_sysfs_path(parent));
				break;
			}

			/* check next parent */
			next = g_udev_device_get_parent(parent);
			g_set_object(&parent, next);
		}
	} else if (g_strcmp0(device_bus, "PCI") == 0) {
		/* the first parent in the 'pci' subsystem is our physical device */
		parent = g_udev_device_get_parent(device);
		while (parent != NULL) {
			g_autoptr(GUdevDevice) next = NULL;

			if (g_strcmp0(g_udev_device_get_subsystem(parent), "pci") == 0) {
				device_sysfs_path = g_strdup(g_udev_device_get_sysfs_path(parent));
				break;
			}

			/* check next parent */
			next = g_udev_device_get_parent(parent);
			g_set_object(&parent, next);
		}
	} else {
		/* other subsystems, we don't support firmware upgrade for those */
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_SUPPORTED,
			    "device bus unsupported: %s",
			    device_bus);
		return FALSE;
	}

	if (device_sysfs_path == NULL) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_FOUND,
			    "failed to lookup device info: physical device not found");
		return FALSE;
	}

	if (out_port_usb_ifnum != NULL)
		*out_port_usb_ifnum = port_usb_ifnum;
	if (out_device_sysfs_path != NULL)
		*out_device_sysfs_path = g_steal_pointer(&device_sysfs_path);
	if (out_device_bus != NULL)
		*out_device_bus = g_steal_pointer(&device_bus);
	return TRUE;
}

gboolean
fu_mm_utils_get_port_info(const gchar *path,
			  gchar **out_device_bus,
			  gchar **out_device_sysfs_path,
			  gint *out_port_usb_ifnum,
			  GError **error)
{
	g_autoptr(GUdevClient) client = NULL;
	g_autoptr(GUdevDevice) dev = NULL;

	client = g_udev_client_new(NULL);
	dev = g_udev_client_query_by_device_file(client, path);
	if (dev == NULL) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_FOUND,
			    "failed to lookup device by path");
		return FALSE;
	}

	return fu_mm_utils_get_udev_port_info(dev,
					      out_device_bus,
					      out_device_sysfs_path,
					      out_port_usb_ifnum,
					      error);
}

gboolean
fu_mm_utils_find_device_file(const gchar *device_sysfs_path,
			     const gchar *subsystem,
			     gchar **out_device_file,
			     GError **error)
{
	GList *devices;
	g_autoptr(GUdevClient) client = NULL;
	g_autofree gchar *device_file = NULL;

	g_return_val_if_fail(out_device_file != NULL, FALSE);

	client = g_udev_client_new(NULL);
	devices = g_udev_client_query_by_subsystem(client, subsystem);
	for (GList *l = devices; l != NULL; l = g_list_next(l)) {
		if (g_str_has_prefix(g_udev_device_get_sysfs_path(G_UDEV_DEVICE(l->data)),
				     device_sysfs_path)) {
			device_file = g_strdup(g_udev_device_get_device_file(l->data));
			if (device_file != NULL)
				break;
		}
	}
	g_list_free_full(devices, g_object_unref);

	if (device_file == NULL) {
		g_set_error(error,
			    G_IO_ERROR,
			    G_IO_ERROR_NOT_FOUND,
			    "failed to find %s port in device %s",
			    subsystem,
			    device_sysfs_path);
		return FALSE;
	}

	*out_device_file = g_steal_pointer(&device_file);

	return TRUE;
}

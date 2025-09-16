/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fu-devlink-backend.h"
#include "fu-devlink-device.h"

struct _FuDevlinkBackend {
	FuBackend parent_instance;
};

G_DEFINE_TYPE(FuDevlinkBackend, fu_devlink_backend, FU_TYPE_BACKEND)

static FuDevice *
fu_devlink_backend_create_pci_parent(FuDevlinkBackend *self,
				     const gchar *bus_name,
				     const gchar *dev_name,
				     GError **error)
{
	FuContext *ctx = fu_backend_get_context(FU_BACKEND(self));
	FuBackend *udev_backend = NULL;
	g_autofree gchar *pci_sysfs_path = NULL;
	g_autofree gchar *pci_sysfs_real = NULL;
	g_autoptr(FuDevice) pci_device = NULL;
	g_autoptr(GError) error_local = NULL;

	/* get the udev backend to create PCI parent device */
	udev_backend = fu_context_get_backend_by_name(ctx, "udev", &error_local);
	if (udev_backend == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "udev backend not available: %s",
			    error_local->message);
		return NULL;
	}

	/* construct PCI sysfs path from bus_name (e.g., "pci/0000:01:00.0") */
	pci_sysfs_path = g_strdup_printf("/sys/bus/pci/devices/%s", dev_name);
	pci_sysfs_real = fu_path_make_absolute(pci_sysfs_path, error);
	if (pci_sysfs_real == NULL)
		return NULL;

	/* create PCI device from sysfs path */
	pci_device = fu_backend_create_device(udev_backend, pci_sysfs_real, &error_local);
	if (pci_device == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "failed to create PCI device for %s: %s",
			    pci_sysfs_path,
			    error_local->message);
		return NULL;
	}

	/* ensure PCI device is probed to get vendor/device info */
	if (!fu_device_probe(pci_device, error)) {
		g_prefix_error_literal(error, "failed to probe PCI device: ");
		return NULL;
	}

	return g_steal_pointer(&pci_device);
}

static FuDevice *
fu_devlink_backend_create_netdevsim_parent(FuDevlinkBackend *self,
					   const gchar *bus_name,
					   const gchar *dev_name,
					   GError **error)
{
	FuContext *ctx = fu_backend_get_context(FU_BACKEND(self));
	g_autoptr(FuDevice) netdevsim_device = NULL;
	g_autofree gchar *physical_id = NULL;

	/* create a fake netdevsim parent device for testing */
	netdevsim_device = fu_device_new(ctx);

	/* set netdevsim device properties */
	physical_id = g_strdup_printf("netdevsim-%s", dev_name);
	fu_device_set_physical_id(netdevsim_device, physical_id);
	fu_device_set_name(netdevsim_device, "Network Device Simulator");

	return g_steal_pointer(&netdevsim_device);
}

gboolean
fu_devlink_backend_device_added(FuDevlinkBackend *self,
				const gchar *bus_name,
				const gchar *dev_name,
				GError **error)
{
	FuContext *ctx = fu_backend_get_context(FU_BACKEND(self));
	g_autoptr(FuDevice) devlink_device = NULL;
	g_autoptr(FuDevice) parent_device = NULL;

	g_return_val_if_fail(FU_IS_DEVLINK_BACKEND(self), FALSE);
	g_return_val_if_fail(bus_name != NULL, FALSE);
	g_return_val_if_fail(dev_name != NULL, FALSE);
	g_return_val_if_fail(error == NULL || *error == NULL, FALSE);

	/* only support PCI and netdevsim buses */
	if (g_strcmp0(bus_name, "pci") == 0) {
		parent_device =
		    fu_devlink_backend_create_pci_parent(self, bus_name, dev_name, error);
		if (parent_device == NULL)
			return FALSE;
	} else if (g_strcmp0(bus_name, "netdevsim") == 0) {
		parent_device =
		    fu_devlink_backend_create_netdevsim_parent(self, bus_name, dev_name, error);
		if (parent_device == NULL)
			return FALSE;
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported bus type: %s (only 'pci' and 'netdevsim' are supported)",
			    bus_name);
		return FALSE;
	}

	/* create devlink device */
	devlink_device = fu_devlink_device_new(ctx, bus_name, dev_name);
	if (devlink_device == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to create devlink device");
		return FALSE;
	}

	/* only add one device for the PCI card -- it does not matter which one we find first */
	if (parent_device != NULL) {
		g_autofree gchar *backend_id = g_strdup(fu_device_get_backend_id(parent_device));
		gchar *tok = g_strrstr(backend_id, ".");
		if (tok != NULL)
			*tok = '\0';
		fu_device_set_backend_id(devlink_device, backend_id);
	}

	/* incorporate information from parent device (without setting hierarchy) */
	fu_device_incorporate(devlink_device,
			      parent_device,
			      FU_DEVICE_INCORPORATE_FLAG_BASECLASS |
				  FU_DEVICE_INCORPORATE_FLAG_VENDOR |
				  FU_DEVICE_INCORPORATE_FLAG_VENDOR_IDS |
				  FU_DEVICE_INCORPORATE_FLAG_VID | FU_DEVICE_INCORPORATE_FLAG_PID);

	/* only add the devlink device to the backend - parent is managed by its own backend */
	fu_backend_device_added(FU_BACKEND(self), devlink_device);

	return TRUE;
}

void
fu_devlink_backend_device_removed(FuDevlinkBackend *self,
				  const gchar *bus_name,
				  const gchar *dev_name)
{
	FuDevice *devlink_device;
	g_autofree gchar *backend_id = g_strdup_printf("%s/%s", bus_name, dev_name);

	g_return_if_fail(FU_IS_DEVLINK_BACKEND(self));
	g_return_if_fail(bus_name != NULL);
	g_return_if_fail(dev_name != NULL);

	devlink_device = fu_backend_lookup_by_id(FU_BACKEND(self), backend_id);
	if (devlink_device == NULL)
		return;

	fu_backend_device_removed(FU_BACKEND(self), devlink_device);
}

static void
fu_devlink_backend_init(FuDevlinkBackend *self)
{
}

static void
fu_devlink_backend_class_init(FuDevlinkBackendClass *klass)
{
}

FuBackend *
fu_devlink_backend_new(FuContext *ctx)
{
	return FU_BACKEND(g_object_new(FU_TYPE_DEVLINK_BACKEND,
				       "name",
				       "devlink",
				       "context",
				       ctx,
				       "device-gtype",
				       FU_TYPE_DEVLINK_DEVICE,
				       NULL));
}

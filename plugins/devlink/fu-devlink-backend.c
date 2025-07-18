/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include "fwupd.h"

#include "fu-devlink-backend.h"
#include "fu-devlink-device.h"

struct _FuDevlinkBackend {
	FuBackend parent_instance;
};

G_DEFINE_TYPE(FuDevlinkBackend, fu_devlink_backend, FU_TYPE_BACKEND)

static void
fu_devlink_backend_to_string(FuBackend *backend, guint idt, GString *str)
{
	/* no specific state to display */
}

static FuDevice *
fu_devlink_backend_create_pci_parent(FuDevlinkBackend *self,
				     const gchar *bus_name,
				     const gchar *dev_name,
				     GError **error)
{
	FuContext *ctx = fu_backend_get_context(FU_BACKEND(self));
	FuBackend *udev_backend = NULL;
	g_autofree gchar *pci_sysfs_path = NULL;
	g_autoptr(FuDevice) pci_device = NULL;
	g_autoptr(GError) error_local = NULL;

	/* Get the udev backend to create PCI parent device */
	udev_backend = fu_context_get_backend_by_name(ctx, "udev", &error_local);
	if (udev_backend == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "udev backend not available: %s",
			    error_local->message);
		return NULL;
	}

	/* Construct PCI sysfs path from bus_name (e.g., "pci/0000:01:00.0") */
	pci_sysfs_path = g_strdup_printf("/sys/bus/pci/devices/%s", dev_name);

	/* Create PCI device from sysfs path */
	pci_device = fu_backend_create_device(udev_backend, pci_sysfs_path, &error_local);
	if (pci_device == NULL) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_FOUND,
			    "failed to create PCI device for %s: %s",
			    pci_sysfs_path,
			    error_local->message);
		return NULL;
	}

	/* Ensure PCI device is probed to get vendor/device info */
	if (!fu_device_probe(pci_device, error)) {
		g_prefix_error(error, "failed to probe PCI device: ");
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

	/* Create a fake netdevsim parent device for testing */
	netdevsim_device = fu_device_new(ctx);

	/* Set netdevsim device properties */
	physical_id = g_strdup_printf("netdevsim-%s", dev_name);
	fu_device_set_physical_id(netdevsim_device, physical_id);
	fu_device_set_name(netdevsim_device, "Network Device Simulator");
	fu_device_add_flag(netdevsim_device, FWUPD_DEVICE_FLAG_EMULATED);

	return g_steal_pointer(&netdevsim_device);
}

FuDevice *
fu_devlink_backend_device_added(FuDevlinkBackend *self,
				const gchar *bus_name,
				const gchar *dev_name,
				GError **error)
{
	FuContext *ctx = fu_backend_get_context(FU_BACKEND(self));
	g_autoptr(FuDevlinkDevice) devlink_device = NULL;
	g_autoptr(FuDevice) parent_device = NULL;
	g_autoptr(FuDeviceLocker) locker = NULL;

	g_return_val_if_fail(FU_IS_DEVLINK_BACKEND(self), NULL);
	g_return_val_if_fail(bus_name != NULL, NULL);
	g_return_val_if_fail(dev_name != NULL, NULL);
	g_return_val_if_fail(error == NULL || *error == NULL, NULL);

	/* Only support PCI and netdevsim buses */
	if (g_strcmp0(bus_name, "pci") == 0) {
		parent_device =
		    fu_devlink_backend_create_pci_parent(self, bus_name, dev_name, error);
		if (parent_device == NULL)
			return NULL;
	} else if (g_strcmp0(bus_name, "netdevsim") == 0) {
		parent_device =
		    fu_devlink_backend_create_netdevsim_parent(self, bus_name, dev_name, error);
		if (parent_device == NULL)
			return NULL;
	} else {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "unsupported bus type: %s (only 'pci' and 'netdevsim' are supported)",
			    bus_name);
		return NULL;
	}

	/* Create devlink device */
	devlink_device = fu_devlink_device_new(ctx, bus_name, dev_name);
	if (devlink_device == NULL) {
		g_set_error_literal(error,
				    FWUPD_ERROR,
				    FWUPD_ERROR_INTERNAL,
				    "failed to create devlink device");
		return NULL;
	}

	/* Incorporate vendor information from parent device (without setting hierarchy) */
	fu_device_incorporate(FU_DEVICE(devlink_device),
			      parent_device,
			      FU_DEVICE_INCORPORATE_FLAG_VENDOR |
				  FU_DEVICE_INCORPORATE_FLAG_VENDOR_IDS |
				  FU_DEVICE_INCORPORATE_FLAG_VID | FU_DEVICE_INCORPORATE_FLAG_PID);

	/* Open device to trigger setup */
	locker = fu_device_locker_new(FU_DEVICE(devlink_device), error);
	if (locker == NULL) {
		g_prefix_error(error, "failed to open devlink device: ");
		return NULL;
	}

	/* Only add the devlink device to the backend - parent is managed by its own backend */
	fu_backend_device_added(FU_BACKEND(self), FU_DEVICE(devlink_device));

	return FU_DEVICE(g_steal_pointer(&devlink_device));
}

static gboolean
fu_devlink_backend_setup(FuBackend *backend,
			 FuBackendSetupFlags flags,
			 FuProgress *progress,
			 GError **error)
{
	/* No specific setup needed for devlink backend */
	return TRUE;
}

static gboolean
fu_devlink_backend_coldplug(FuBackend *backend, FuProgress *progress, GError **error)
{
	/* Device discovery is handled by the plugin via netlink */
	return TRUE;
}

static void
fu_devlink_backend_init(FuDevlinkBackend *self)
{
	/* No initialization needed */
}

static void
fu_devlink_backend_class_init(FuDevlinkBackendClass *klass)
{
	FuBackendClass *backend_class = FU_BACKEND_CLASS(klass);
	backend_class->setup = fu_devlink_backend_setup;
	backend_class->coldplug = fu_devlink_backend_coldplug;
	backend_class->to_string = fu_devlink_backend_to_string;
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

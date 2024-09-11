/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuPciDevice"

#include "config.h"

#include "fu-pci-device.h"

/**
 * FuPciDevice
 *
 * See also: #FuUdevDevice
 */

G_DEFINE_TYPE(FuPciDevice, fu_pci_device, FU_TYPE_UDEV_DEVICE)

static gboolean
fu_pci_device_probe(FuDevice *device, GError **error)
{
	FuPciDevice *self = FU_PCI_DEVICE(device);
	g_autofree gchar *physical_id = NULL;
	g_autofree gchar *prop_slot = NULL;

	/* FuUdevDevice->probe */
	if (!FU_DEVICE_CLASS(fu_pci_device_parent_class)->probe(device, error))
		return FALSE;

	/* physical slot */
	prop_slot = fu_udev_device_read_property(FU_UDEV_DEVICE(self), "PCI_SLOT_NAME", error);
	if (prop_slot == NULL)
		return FALSE;
	physical_id = g_strdup_printf("PCI_SLOT_NAME=%s", prop_slot);
	fu_device_set_physical_id(device, physical_id);

	/* success */
	return TRUE;
}

static void
fu_pci_device_init(FuPciDevice *self)
{
}

static void
fu_pci_device_class_init(FuPciDeviceClass *klass)
{
	FuDeviceClass *device_class = FU_DEVICE_CLASS(klass);
	device_class->probe = fu_pci_device_probe;
}

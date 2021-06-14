/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_PCI_DEVICE (fu_pci_device_get_type ())
G_DECLARE_DERIVABLE_TYPE (FuPciDevice, fu_pci_device, FU, PCI_DEVICE, FuDevice)

struct _FuPciDeviceClass
{
	FuDeviceClass		parent_class;
};

FuDevice	*fu_pci_device_new			(const gchar	*bdf,
							 GError		**error);
guint32		 fu_pci_device_read_config		(FuPciDevice	*self,
							 guint32	 addr);

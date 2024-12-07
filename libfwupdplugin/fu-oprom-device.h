/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-pci-device.h"

#define FU_TYPE_OPROM_DEVICE (fu_oprom_device_get_type())

G_DECLARE_DERIVABLE_TYPE(FuOpromDevice, fu_oprom_device, FU, OPROM_DEVICE, FuPciDevice)

struct _FuOpromDeviceClass {
	FuPciDeviceClass parent_class;
};

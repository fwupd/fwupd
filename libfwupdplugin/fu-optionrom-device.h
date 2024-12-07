/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-pci-device.h"

#define FU_TYPE_OPTIONROM_DEVICE (fu_optionrom_device_get_type())

G_DECLARE_DERIVABLE_TYPE(FuOptionromDevice, fu_optionrom_device, FU, OPTIONROM_DEVICE, FuPciDevice)

struct _FuOptionromDeviceClass {
	FuPciDeviceClass parent_class;
};

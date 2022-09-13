/*
 * Copyright (C) 2022 Advanced Micro Devices Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_PCI_PSP_DEVICE (fu_pci_psp_device_get_type())
G_DECLARE_FINAL_TYPE(FuPciPspDevice, fu_pci_psp_device, FU, PCI_PSP_DEVICE, FuUdevDevice)

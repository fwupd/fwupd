/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SYNAPTICS_VMM9_DEVICE (fu_synaptics_vmm9_device_get_type())
G_DECLARE_FINAL_TYPE(FuSynapticsVmm9Device,
		     fu_synaptics_vmm9_device,
		     FU,
		     SYNAPTICS_VMM9_DEVICE,
		     FuHidDevice)

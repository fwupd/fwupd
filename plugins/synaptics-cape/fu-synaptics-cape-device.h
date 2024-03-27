/*
 * Copyright 2021 Synaptics Incorporated <simon.ho@synaptics.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_SYNAPTICS_CAPE_DEVICE (fu_synaptics_cape_device_get_type())
G_DECLARE_FINAL_TYPE(FuSynapticsCapeDevice,
		     fu_synaptics_cape_device,
		     FU,
		     SYNAPTICS_CAPE_DEVICE,
		     FuHidDevice)

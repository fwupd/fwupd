/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_SYNAPTICS_MST_DEVICE (fu_synaptics_mst_device_get_type ())
G_DECLARE_FINAL_TYPE (FuSynapticsMstDevice, fu_synaptics_mst_device, FU, SYNAPTICS_MST_DEVICE, FuUdevDevice)

FuSynapticsMstDevice	*fu_synaptics_mst_device_new	(FuUdevDevice	*device);
void	 fu_synaptics_mst_device_set_system_type	(FuSynapticsMstDevice	*self,
						 const gchar 		*system_type);

/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_SYNAPTICSMST_DEVICE (fu_synapticsmst_device_get_type ())
G_DECLARE_FINAL_TYPE (FuSynapticsmstDevice, fu_synapticsmst_device, FU, SYNAPTICSMST_DEVICE, FuUdevDevice)

FuSynapticsmstDevice	*fu_synapticsmst_device_new	(FuUdevDevice	*device);
void	 fu_synapticsmst_device_set_system_type	(FuSynapticsmstDevice	*self,
						 const gchar 		*system_type);

G_END_DECLS

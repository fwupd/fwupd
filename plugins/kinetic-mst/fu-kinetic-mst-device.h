/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2021 Jeffrey Lin <jlin@kinet-ic.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

#define FU_TYPE_KINETIC_MST_DEVICE (fu_kinetic_mst_device_get_type ())
G_DECLARE_FINAL_TYPE (FuKineticMstDevice, fu_kinetic_mst_device, FU, KINETIC_MST_DEVICE, FuUdevDevice)

FuKineticMstDevice *fu_kinetic_mst_device_new (FuUdevDevice	*device);
void fu_kinetic_mst_device_set_system_type	(FuKineticMstDevice	*self,
                                                        const gchar *system_type);

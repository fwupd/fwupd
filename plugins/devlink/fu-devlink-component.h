/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DEVLINK_COMPONENT (fu_devlink_component_get_type())
G_DECLARE_FINAL_TYPE(FuDevlinkComponent, fu_devlink_component, FU, DEVLINK_COMPONENT, FuDevice)

FuDevice *
fu_devlink_component_new(FuContext *ctx, const gchar *component_name) G_GNUC_NON_NULL(1, 2);

void
fu_devlink_component_build_instance_id(FuDevice *device,
				       FuDevice *parent,
				       const gchar *driver_name,
				       GHashTable *version_table) G_GNUC_NON_NULL(1, 2, 4);

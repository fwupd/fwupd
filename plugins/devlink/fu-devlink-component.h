/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DEVLINK_COMPONENT (fu_devlink_component_get_type())
G_DECLARE_FINAL_TYPE(FuDevlinkComponent, fu_devlink_component, FU, DEVLINK_COMPONENT, FuDevice)

void
fu_devlink_component_add_instance_keys(FuDevice *device, gchar **keys) G_GNUC_NON_NULL(1, 2);

FuDevice *
fu_devlink_component_new(FuDevice *proxy, const gchar *logical_id) G_GNUC_NON_NULL(1, 2);

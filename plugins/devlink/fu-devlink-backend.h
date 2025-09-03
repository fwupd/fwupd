/*
 * Copyright 2025 NVIDIA Corporation & Affiliates
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_DEVLINK_BACKEND (fu_devlink_backend_get_type())
G_DECLARE_FINAL_TYPE(FuDevlinkBackend, fu_devlink_backend, FU, DEVLINK_BACKEND, FuBackend)

FuBackend *
fu_devlink_backend_new(FuContext *ctx) G_GNUC_NON_NULL(1);

gboolean
fu_devlink_backend_device_added(FuDevlinkBackend *self,
				const gchar *bus_name,
				const gchar *dev_name,
				GError **error) G_GNUC_NON_NULL(1, 2, 3);

void
fu_devlink_backend_device_removed(FuDevlinkBackend *self,
				  const gchar *bus_name,
				  const gchar *dev_name) G_GNUC_NON_NULL(1, 2, 3);

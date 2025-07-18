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
fu_devlink_backend_new(FuContext *ctx);

FuDevice *
fu_devlink_backend_device_added(FuDevlinkBackend *self,
                                const gchar *bus_name,
                                const gchar *dev_name,
                                GError **error);

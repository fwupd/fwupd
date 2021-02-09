/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-backend.h"

#define FU_TYPE_UDEV_BACKEND (fu_udev_backend_get_type ())
G_DECLARE_FINAL_TYPE (FuUdevBackend, fu_udev_backend, FU, UDEV_BACKEND, FuBackend)

FuBackend	*fu_udev_backend_new		(GPtrArray	*subsystems);

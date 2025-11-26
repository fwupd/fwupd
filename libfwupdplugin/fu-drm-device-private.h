/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-drm-device.h"

void
fu_drm_device_set_edid(FuDrmDevice *self, FuEdid *edid) G_GNUC_NON_NULL(1);

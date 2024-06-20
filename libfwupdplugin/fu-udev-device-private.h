/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-udev-device.h"

void
fu_udev_device_emit_changed(FuUdevDevice *self) G_GNUC_NON_NULL(1);

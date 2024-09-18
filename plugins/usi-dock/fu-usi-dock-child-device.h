/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_USI_DOCK_CHILD_DEVICE (fu_usi_dock_child_device_get_type())
G_DECLARE_FINAL_TYPE(FuUsiDockChildDevice,
		     fu_usi_dock_child_device,
		     FU,
		     USI_DOCK_CHILD_DEVICE,
		     FuDevice)

FuDevice *
fu_usi_dock_child_device_new(FuContext *ctx);
void
fu_usi_dock_child_device_set_chip_idx(FuUsiDockChildDevice *self, guint8 chip_idx);

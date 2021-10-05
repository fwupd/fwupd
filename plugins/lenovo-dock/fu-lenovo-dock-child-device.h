/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LENOVO_DOCK_CHILD_DEVICE (fu_lenovo_dock_child_device_get_type())
G_DECLARE_FINAL_TYPE(FuLenovoDockChildDevice,
		     fu_lenovo_dock_child_device,
		     FU,
		     LENOVO_DOCK_CHILD_DEVICE,
		     FuDevice)

FuDevice *
fu_lenovo_dock_child_new(FuContext *ctx);
void
fu_lenovo_dock_child_device_set_chip_idx(FuLenovoDockChildDevice *self, guint8 chip_idx);

/*
 * Copyright (C) 2022 Google LLC
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_VBE_DEVICE (fu_vbe_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuVbeDevice, fu_vbe_device, FU, VBE_DEVICE, FuDevice)

struct _FuVbeDeviceClass {
	FuDeviceClass parent_class;
};

FuFdtImage *
fu_vbe_device_get_fdt_root(FuVbeDevice *self);
FuFdtImage *
fu_vbe_device_get_fdt_node(FuVbeDevice *self);
gchar **
fu_vbe_device_get_compatible(FuVbeDevice *self);
const gchar *
fu_vbe_device_get_dir(FuVbeDevice *self);

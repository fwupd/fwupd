/*
 * VBE plugin for fwupd,mmc-simple
 *
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

typedef FuDevice *(*vbe_device_new_func)(FuContext *ctx,
					 const gchar *vbe_method,
					 const gchar *fdt,
					 gint node,
					 const gchar *vbe_dir);

const gchar *
fu_vbe_device_get_method(FuVbeDevice *self);

gpointer
fu_vbe_device_get_fdt(FuVbeDevice *self);

gint
fu_vbe_device_get_node(FuVbeDevice *self);

GList *
fu_vbe_device_get_compat_list(FuVbeDevice *self);

const gchar *
fu_vbe_device_get_dir(FuVbeDevice *self);

/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_THUNDERBOLT_DEVICE (fu_thunderbolt_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuThunderboltDevice,
			 fu_thunderbolt_device,
			 FU,
			 THUNDERBOLT_DEVICE,
			 FuUdevDevice)

struct _FuThunderboltDeviceClass {
	FuUdevDeviceClass parent_class;
};

gboolean
fu_thunderbolt_device_get_version(FuThunderboltDevice *self, GError **error);
GFile *
fu_thunderbolt_device_find_nvmem(FuThunderboltDevice *self, gboolean active, GError **error);
gboolean
fu_thunderbolt_device_check_authorized(FuThunderboltDevice *self, GError **error);
void
fu_thunderbolt_device_set_auth_method(FuThunderboltDevice *self, const gchar *auth_method);

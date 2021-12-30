/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2020 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

gboolean
fu_thunderbolt_udev_set_port_online(FuUdevDevice *device, GError **error);
gboolean
fu_thunderbolt_udev_set_port_offline(FuUdevDevice *device, GError **error);
guint16
fu_thunderbolt_udev_get_attr_uint16(FuUdevDevice *device, const gchar *name, GError **error);

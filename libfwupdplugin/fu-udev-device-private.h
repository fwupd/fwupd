/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-udev-device.h"

void
fu_udev_device_emit_changed(FuUdevDevice *self) G_GNUC_NON_NULL(1);
FuUdevDevice *
fu_udev_device_new(FuContext *ctx, const gchar *sysfs_path);
void
fu_udev_device_set_io_channel(FuUdevDevice *self, FuIOChannel *io_channel) G_GNUC_NON_NULL(1, 2);
void
fu_udev_device_set_number(FuUdevDevice *self, guint64 number) G_GNUC_NON_NULL(1);
void
fu_udev_device_set_subsystem(FuUdevDevice *self, const gchar *subsystem) G_GNUC_NON_NULL(1);
void
fu_udev_device_add_property(FuUdevDevice *self, const gchar *key, const gchar *value);
gboolean
fu_udev_device_parse_number(FuUdevDevice *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_udev_device_match_subsystem(FuUdevDevice *self, const gchar *subsystem) G_GNUC_NON_NULL(1);
gchar *
fu_udev_device_get_device_file_from_subsystem(FuUdevDevice *self,
					      const gchar *subsystem,
					      GError **error) G_GNUC_NON_NULL(1, 2);

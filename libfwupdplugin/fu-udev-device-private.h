/*
 * Copyright 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-udev-device.h"

void
fu_udev_device_emit_changed(FuUdevDevice *self) G_GNUC_NON_NULL(1);
void
fu_udev_device_set_io_channel(FuUdevDevice *self, FuIOChannel *io_channel) G_GNUC_NON_NULL(1, 2);
void
fu_udev_device_set_number(FuUdevDevice *self, guint64 number) G_GNUC_NON_NULL(1);
void
fu_udev_device_set_subsystem(FuUdevDevice *self, const gchar *subsystem) G_GNUC_NON_NULL(1);
gboolean
fu_udev_device_parse_number(FuUdevDevice *self, GError **error) G_GNUC_NON_NULL(1);
gboolean
fu_udev_device_match_subsystem(FuUdevDevice *self, const gchar *subsystem) G_GNUC_NON_NULL(1);
void
fu_udev_device_set_dev(FuUdevDevice *self, GUdevDevice *udev_device) G_GNUC_NON_NULL(1);

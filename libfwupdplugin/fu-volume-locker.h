/*
 * Copyright 2025 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-volume.h"

#define FU_TYPE_VOLUME_LOCKER (fu_volume_locker_get_type())

G_DECLARE_FINAL_TYPE(FuVolumeLocker, fu_volume_locker, FU, VOLUME_LOCKER, GObject)

FuVolumeLocker *
fu_volume_locker_new(FuVolume *volume, GError **error) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
gboolean
fu_volume_locker_close(FuVolumeLocker *self, GError **error) G_GNUC_WARN_UNUSED_RESULT
    G_GNUC_NON_NULL(1);

/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 * Copyright 2019 Mario Limonciello <mario.limonciello@dell.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <gio/gio.h>

#include "fu-volume.h"

FuVolume *
fu_volume_new_from_mount_path(const gchar *mount_path) G_GNUC_NON_NULL(1);

void
fu_volume_set_partition_kind(FuVolume *self, const gchar *partition_kind) G_GNUC_NON_NULL(1, 2);
void
fu_volume_set_partition_uuid(FuVolume *self, const gchar *partition_uuid) G_GNUC_NON_NULL(1, 2);

/* for tests */
void
fu_volume_set_filesystem_free(FuVolume *self, guint64 filesystem_free) G_GNUC_NON_NULL(1);

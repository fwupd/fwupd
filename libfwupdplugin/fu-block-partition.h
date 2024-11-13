/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-block-device.h"

#define FU_TYPE_BLOCK_PARTITION (fu_block_partition_get_type())
G_DECLARE_DERIVABLE_TYPE(FuBlockPartition, fu_block_partition, FU, BLOCK_PARTITION, FuBlockDevice)

struct _FuBlockPartitionClass {
	FuBlockDeviceClass parent_class;
};

const gchar *
fu_block_partition_get_fs_type(FuBlockPartition *self) G_GNUC_NON_NULL(1);
const gchar *
fu_block_partition_get_fs_uuid(FuBlockPartition *self) G_GNUC_NON_NULL(1);
const gchar *
fu_block_partition_get_fs_label(FuBlockPartition *self) G_GNUC_NON_NULL(1);
gchar *
fu_block_partition_get_mount_point(FuBlockPartition *self, GError **error) G_GNUC_NON_NULL(1);

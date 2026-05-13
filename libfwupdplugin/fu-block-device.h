/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-udev-device.h"

#define FU_TYPE_BLOCK_DEVICE (fu_block_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuBlockDevice, fu_block_device, FU, BLOCK_DEVICE, FuUdevDevice)

struct _FuBlockDeviceClass {
	FuUdevDeviceClass parent_class;
};

gboolean
fu_block_device_sg_io_cmd_none(FuBlockDevice *self, const guint8 *cdb, guint8 cdbsz, GError **error)
    G_GNUC_NON_NULL(1, 2);
gboolean
fu_block_device_sg_io_cmd_read(FuBlockDevice *self,
			       const guint8 *cdb,
			       gsize cdbsz,
			       guint8 *buf,
			       gsize bufsz,
			       GError **error) G_GNUC_NON_NULL(1, 2, 4);
gboolean
fu_block_device_sg_io_cmd_write(FuBlockDevice *self,
				const guint8 *cdb,
				gsize cdbsz,
				const guint8 *buf,
				gsize bufsz,
				GError **error) G_GNUC_NON_NULL(1, 2, 4);

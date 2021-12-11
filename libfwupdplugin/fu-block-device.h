/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-device.h"

#define FU_TYPE_BLOCK_DEVICE (fu_block_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuBlockDevice, fu_block_device, FU, BLOCK_DEVICE, FuDevice)

struct _FuBlockDeviceClass {
	FuDeviceClass parent_class;
	gpointer __reserved[31];
};

void
fu_block_device_set_uuid(FuBlockDevice *self, const gchar *uuid);
const gchar *
fu_block_device_get_uuid(FuBlockDevice *self);
void
fu_block_device_set_label(FuBlockDevice *self, const gchar *label);
const gchar *
fu_block_device_get_label(FuBlockDevice *self);
void
fu_block_device_set_filename(FuBlockDevice *self, const gchar *filename);
const gchar *
fu_block_device_get_filename(FuBlockDevice *self);

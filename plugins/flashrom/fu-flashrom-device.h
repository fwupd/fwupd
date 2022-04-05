/*
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

struct _FuFlashromDeviceClass {
	FuUdevDeviceClass parent_class;
};

#define FU_TYPE_FLASHROM_DEVICE (fu_flashrom_device_get_type())
G_DECLARE_DERIVABLE_TYPE(FuFlashromDevice, fu_flashrom_device, FU, FLASHROM_DEVICE, FuUdevDevice)

void
fu_flashrom_device_set_programmer_name(FuFlashromDevice *self, const gchar *name);
const gchar *
fu_flashrom_device_get_programmer_name(FuFlashromDevice *self);
void
fu_flashrom_device_set_programmer_args(FuFlashromDevice *self, const gchar *args);
struct flashrom_flashctx *
fu_flashrom_device_get_flashctx(FuFlashromDevice *self);

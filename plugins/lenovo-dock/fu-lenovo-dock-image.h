/*
 * Copyright 2026 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LENOVO_DOCK_IMAGE (fu_lenovo_dock_image_get_type())
G_DECLARE_FINAL_TYPE(FuLenovoDockImage, fu_lenovo_dock_image, FU, LENOVO_DOCK_IMAGE, FuFirmware)

#define FU_LENOVO_DOCK_DEVICE_SIGNATURE_SIZE 256

FuFirmware *
fu_lenovo_dock_image_new(void);
guint32
fu_lenovo_dock_image_get_crc(FuLenovoDockImage *self);
void
fu_lenovo_dock_image_set_crc(FuLenovoDockImage *self, guint32 crc);

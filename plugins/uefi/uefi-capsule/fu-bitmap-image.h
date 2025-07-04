/*
 * Copyright 2024 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_BITMAP_IMAGE (fu_bitmap_image_get_type())
G_DECLARE_FINAL_TYPE(FuBitmapImage, fu_bitmap_image, FU, BITMAP_IMAGE, FuFirmware)

FuBitmapImage *
fu_bitmap_image_new(void);
guint32
fu_bitmap_image_get_width(FuBitmapImage *self);
guint32
fu_bitmap_image_get_height(FuBitmapImage *self);

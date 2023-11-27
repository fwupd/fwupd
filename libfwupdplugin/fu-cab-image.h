/*
 * Copyright (C) 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_CAB_IMAGE (fu_cab_image_get_type())

G_DECLARE_FINAL_TYPE(FuCabImage, fu_cab_image, FU, CAB_IMAGE, FuFirmware)

const gchar *
fu_cab_image_get_win32_filename(FuCabImage *self) G_GNUC_NON_NULL(1);
void
fu_cab_image_set_win32_filename(FuCabImage *self, const gchar *win32_filename) G_GNUC_NON_NULL(1);
GDateTime *
fu_cab_image_get_created(FuCabImage *self) G_GNUC_NON_NULL(1);
void
fu_cab_image_set_created(FuCabImage *self, GDateTime *created) G_GNUC_NON_NULL(1);

FuCabImage *
fu_cab_image_new(void) G_GNUC_WARN_UNUSED_RESULT;

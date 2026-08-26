/*
 * Copyright 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include "fu-firmware.h"

G_BEGIN_DECLS

#define FU_TYPE_FMAP_FIRMWARE (fu_fmap_firmware_get_type())
G_DECLARE_DERIVABLE_TYPE(FuFmapFirmware, fu_fmap_firmware, FU, FMAP_FIRMWARE, FuFirmware)

struct _FuFmapFirmwareClass {
	FuFirmwareClass parent_class;
};

FuFirmware *
fu_fmap_firmware_new(void);
gboolean
fu_fmap_firmware_find(FuFmapFirmware *self,
		      FuInputStream *stream,
		      gsize offset,
		      gsize search_size,
		      gsize image_size,
		      gsize *offset_found,
		      GError **error) G_GNUC_NON_NULL(1, 2, 6) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS

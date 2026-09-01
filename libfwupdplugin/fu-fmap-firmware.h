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

G_END_DECLS

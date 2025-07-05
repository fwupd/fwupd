/*
 * Copyright 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-cros-ec-common.h"
#include "fu-cros-ec-struct.h"

#define FU_TYPE_CROS_EC_FIRMWARE (fu_cros_ec_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuCrosEcFirmware, fu_cros_ec_firmware, FU, CROS_EC_FIRMWARE, FuFmapFirmware)

typedef struct {
	const gchar *name;
	guint32 offset;
	gsize size;
	FuCrosEcFirmwareUpgradeStatus ustatus;
	gchar raw_version[FU_FMAP_FIRMWARE_STRLEN];
	FuCrosEcVersion version;
	gint32 rollback;
	guint32 key_version;
	guint64 image_idx;
} FuCrosEcFirmwareSection;

gboolean
fu_cros_ec_firmware_ensure_version(FuCrosEcFirmware *self, GError **error);
gboolean
fu_cros_ec_firmware_pick_sections(FuCrosEcFirmware *self, guint32 writeable_offset, GError **error);
GPtrArray *
fu_cros_ec_firmware_get_needed_sections(FuCrosEcFirmware *self, GError **error);
FuFirmware *
fu_cros_ec_firmware_new(void);

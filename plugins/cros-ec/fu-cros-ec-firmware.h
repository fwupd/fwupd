/*
 * Copyright (C) 2020 Benson Leung <bleung@chromium.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-cros-ec-common.h"

#define FU_TYPE_CROS_EC_FIRMWARE (fu_cros_ec_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuCrosEcFirmware, fu_cros_ec_firmware, FU, CROS_EC_FIRMWARE, FuFmapFirmware)

/*
 * Each RO or RW section of the new image can be in one of the following
 * states.
 */
typedef enum {
	FU_CROS_EC_FW_NOT_NEEDED= 0,	/* Version below or equal that on the target. */
	FU_CROS_EC_FW_NOT_POSSIBLE,	/*
					 * RO is newer, but can't be transferred due to
					 * target RW shortcomings.
					 */
	FU_CROS_EC_FW_NEEDED		/*
					 * This section needs to be transferred to the
					 * target.
					 */
} FuCrosEcFirmwareUpgradeStatus;

typedef struct {
	const gchar			*name;
	guint32				offset;
	gsize				size;
	FuCrosEcFirmwareUpgradeStatus	ustatus;
	gchar				raw_version[FU_FMAP_FIRMWARE_STRLEN];
	struct cros_ec_version		version;
	gint32				rollback;
	guint32				key_version;
	guint64				image_idx;
} FuCrosEcFirmwareSection;

gboolean			 fu_cros_ec_firmware_pick_sections	(FuCrosEcFirmware *self,
									 guint32 writeable_offset,
									 GError **error);
GPtrArray			*fu_cros_ec_firmware_get_sections	(FuCrosEcFirmware *self);
FuFirmware			*fu_cros_ec_firmware_new		(void);

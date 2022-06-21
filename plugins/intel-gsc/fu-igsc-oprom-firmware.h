/*
 * Copyright (C) 2022 Intel
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_IGSC_OPROM_FIRMWARE (fu_igsc_oprom_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuIgscOpromFirmware,
		     fu_igsc_oprom_firmware,
		     FU,
		     IGSC_OPROM_FIRMWARE,
		     FuOpromFirmware)

FuFirmware *
fu_igsc_oprom_firmware_new(void);

guint16
fu_igsc_oprom_firmware_get_major_version(FuIgscOpromFirmware *self);
gboolean
fu_igsc_oprom_firmware_has_allowlist(FuIgscOpromFirmware *self);

gboolean
fu_igsc_oprom_firmware_match_device(FuIgscOpromFirmware *self,
				    guint16 vendor_id,
				    guint16 device_id,
				    guint16 subsys_vendor_id,
				    guint16 subsys_device_id,
				    GError **error);

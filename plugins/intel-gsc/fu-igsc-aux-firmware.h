/*
 * Copyright (C) 2022 Intel
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_IGSC_AUX_FIRMWARE (fu_igsc_aux_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuIgscAuxFirmware,
		     fu_igsc_aux_firmware,
		     FU,
		     IGSC_AUX_FIRMWARE,
		     FuIfwiFptFirmware)

FuFirmware *
fu_igsc_aux_firmware_new(void);

guint32
fu_igsc_aux_firmware_get_oem_version(FuIgscAuxFirmware *self);
guint16
fu_igsc_aux_firmware_get_major_version(FuIgscAuxFirmware *self);
guint16
fu_igsc_aux_firmware_get_major_vcn(FuIgscAuxFirmware *self);

gboolean
fu_igsc_aux_firmware_match_device(FuIgscAuxFirmware *self,
				  guint16 vendor_id,
				  guint16 device_id,
				  guint16 subsys_vendor_id,
				  guint16 subsys_device_id,
				  GError **error);

/*
 * Copyright 2025 Joe hong <JoeHung@ilitek.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ILITEK_ITS_CAPSULE_FIRMWARE (fu_ilitek_its_capsule_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuIlitekItsCapsuleFirmware,
		     fu_ilitek_its_capsule_firmware,
		     FU,
		     ILITEK_ITS_CAPSULE_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_ilitek_its_capsule_firmware_new(void);

guint8
fu_ilitek_its_capsule_firmware_get_lookup_cnt(FuIlitekItsCapsuleFirmware *self);
guint16
fu_ilitek_its_capsule_firmware_get_lookup_fwid(FuIlitekItsCapsuleFirmware *self, guint8 idx);
guint8
fu_ilitek_its_capsule_firmware_get_lookup_type(FuIlitekItsCapsuleFirmware *self, guint8 idx);
guint32
fu_ilitek_its_capsule_firmware_get_lookup_edid(FuIlitekItsCapsuleFirmware *self, guint8 idx);
guint32
fu_ilitek_its_capsule_firmware_get_lookup_edid_mask(FuIlitekItsCapsuleFirmware *self, guint8 idx);
guint8
fu_ilitek_its_capsule_firmware_get_lookup_sensor_id(FuIlitekItsCapsuleFirmware *self, guint8 idx);
guint8
fu_ilitek_its_capsule_firmware_get_lookup_sensor_id_mask(FuIlitekItsCapsuleFirmware *self,
							 guint8 idx);

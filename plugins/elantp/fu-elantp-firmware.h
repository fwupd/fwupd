/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ELANTP_FIRMWARE (fu_elantp_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuElantpFirmware, fu_elantp_firmware, FU, ELANTP_FIRMWARE, FuFirmware)

FuFirmware	*fu_elantp_firmware_new			(void);
guint16		 fu_elantp_firmware_get_module_id	(FuElantpFirmware	*self);
guint16		 fu_elantp_firmware_get_iap_addr	(FuElantpFirmware	*self);

/*
 * Copyright (C) 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-firmware.h"

#define FU_TYPE_SYNAPTICS_MST_FIRMWARE (fu_synaptics_mst_firmware_get_type ())
G_DECLARE_FINAL_TYPE (FuSynapticsMstFirmware, fu_synaptics_mst_firmware, FU, SYNAPTICS_MST_FIRMWARE, FuFirmware)

FuFirmware	*fu_synaptics_mst_firmware_new		(void);
guint16		 fu_synaptics_mst_firmware_get_board_id	(FuSynapticsMstFirmware	*self);

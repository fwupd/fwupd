/*
 * Copyright 2021 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-synaptics-mst-struct.h"

#define FU_TYPE_SYNAPTICS_MST_FIRMWARE (fu_synaptics_mst_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuSynapticsMstFirmware,
		     fu_synaptics_mst_firmware,
		     FU,
		     SYNAPTICS_MST_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_synaptics_mst_firmware_new(void);
guint16
fu_synaptics_mst_firmware_get_board_id(FuSynapticsMstFirmware *self);
void
fu_synaptics_mst_firmware_set_family(FuSynapticsMstFirmware *self, FuSynapticsMstFamily family);

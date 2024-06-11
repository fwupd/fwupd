/*
 * Copyright 2024 Algoltek <Algoltek, Inc.>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ALGOLTEK_AUX_FIRMWARE (fu_algoltek_aux_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuAlgoltekAuxFirmware,
		     fu_algoltek_aux_firmware,
		     FU,
		     ALGOLTEK_AUX_FIRMWARE,
		     FuFirmware)

#define FU_ALGOLTEK_AUX_FIRMWARE_ISP_SIZE     0x1000
#define FU_ALGOLTEK_AUX_FIRMWARE_PAYLOAD_SIZE 0x20000

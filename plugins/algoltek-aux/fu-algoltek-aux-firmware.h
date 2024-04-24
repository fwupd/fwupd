/*
 * Copyright (C) 2024 Algoltek <Algoltek, Inc.>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ALGOLTEK_AUX_FIRMWARE (fu_algoltek_aux_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuAlgoltekAuxFirmware,
		     fu_algoltek_aux_firmware,
		     FU,
		     ALGOLTEK_AUX_FIRMWARE,
		     FuFirmware)


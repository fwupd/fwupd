/*
 * Copyright 2024 Algoltek <Algoltek, Inc.>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ALGOLTEK_AUX_DEVICE (fu_algoltek_aux_device_get_type())
G_DECLARE_FINAL_TYPE(FuAlgoltekAuxDevice,
		     fu_algoltek_aux_device,
		     FU,
		     ALGOLTEK_AUX_DEVICE,
		     FuDpauxDevice)

/*
 * Copyright 2024 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ALGOLTEK_USBCR_DEVICE (fu_algoltek_usbcr_device_get_type())
G_DECLARE_FINAL_TYPE(FuAlgoltekUsbcrDevice,
		     fu_algoltek_usbcr_device,
		     FU,
		     ALGOLTEK_USBCR_DEVICE,
		     FuBlockDevice)

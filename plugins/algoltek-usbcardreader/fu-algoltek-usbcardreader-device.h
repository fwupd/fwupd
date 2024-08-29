/*
 * Copyright 2024 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ALGOLTEK_USBCARDREADER_DEVICE (fu_algoltek_usbcardreader_device_get_type())
G_DECLARE_FINAL_TYPE(FuAlgoltekUsbcardreaderDevice,
		     fu_algoltek_usbcardreader_device,
		     FU,
		     ALGOLTEK_USBCARDREADER_DEVICE,
		     FuUdevDevice)

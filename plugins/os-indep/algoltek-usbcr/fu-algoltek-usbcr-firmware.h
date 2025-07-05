/*
 * Copyright 2024 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ALGOLTEK_USBCR_FIRMWARE (fu_algoltek_usbcr_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuAlgoltekUsbcrFirmware,
		     fu_algoltek_usbcr_firmware,
		     FU,
		     ALGOLTEK_USBCR_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_algoltek_usbcr_firmware_new(void);
guint16
fu_algoltek_usbcr_firmware_get_boot_ver(FuAlgoltekUsbcrFirmware *self);

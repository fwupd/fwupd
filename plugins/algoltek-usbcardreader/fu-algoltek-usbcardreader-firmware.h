/*
 * Copyright 2024 Algoltek, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ALGOLTEK_USBCARDREADER_FIRMWARE (fu_algoltek_usbcardreader_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuAlgoltekUsbcardreaderFirmware,
		     fu_algoltek_usbcardreader_firmware,
		     FU,
		     ALGOLTEK_USBCARDREADER_FIRMWARE,
		     FuFirmware)

FuFirmware *
fu_algoltek_usbcardreader_firmware_new(void);

guint16
fu_algoltek_usbcardreader_firmware_get_boot_ver(FuAlgoltekUsbcardreaderFirmware *self);

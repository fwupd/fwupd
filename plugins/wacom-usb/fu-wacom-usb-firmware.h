/*
 * Copyright 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_WACOM_USB_FIRMWARE (fu_wacom_usb_firmware_get_type())
G_DECLARE_FINAL_TYPE(FuWacomUsbFirmware, fu_wacom_usb_firmware, FU, WACOM_USB_FIRMWARE, FuFirmware)

FuFirmware *
fu_wacom_usb_firmware_new(void);

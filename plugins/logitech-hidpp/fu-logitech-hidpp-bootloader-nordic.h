/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-logitech-hidpp-bootloader.h"

#define FU_TYPE_UNIFYING_BOOTLOADER_NORDIC (fu_logitech_hidpp_bootloader_nordic_get_type ())
G_DECLARE_FINAL_TYPE (FuLogitechHidPpBootloaderNordic, fu_logitech_hidpp_bootloader_nordic, FU, UNIFYING_BOOTLOADER_NORDIC, FuLogitechHidPpBootloader)

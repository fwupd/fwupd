/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-logitech-hidpp-bootloader.h"

#define FU_TYPE_LOGITECH_HIDPP_BOOTLOADER_NORDIC (fu_logitech_hidpp_bootloader_nordic_get_type())
G_DECLARE_FINAL_TYPE(FuLogitechHidppBootloaderNordic,
		     fu_logitech_hidpp_bootloader_nordic,
		     FU,
		     LOGITECH_HIDPP_BOOTLOADER_NORDIC,
		     FuLogitechHidppBootloader)

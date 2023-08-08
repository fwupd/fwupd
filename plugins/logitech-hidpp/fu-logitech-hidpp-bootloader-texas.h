/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-logitech-hidpp-bootloader.h"

#define FU_TYPE_LOGITECH_HIDPP_BOOTLOADER_TEXAS (fu_logitech_hidpp_bootloader_texas_get_type())
G_DECLARE_FINAL_TYPE(FuLogitechHidppBootloaderTexas,
		     fu_logitech_hidpp_bootloader_texas,
		     FU,
		     LOGITECH_HIDPP_BOOTLOADER_TEXAS,
		     FuLogitechHidppBootloader)

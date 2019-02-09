/*
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-unifying-bootloader.h"

G_BEGIN_DECLS

#define FU_TYPE_UNIFYING_BOOTLOADER_NORDIC (fu_unifying_bootloader_nordic_get_type ())
G_DECLARE_FINAL_TYPE (FuUnifyingBootloaderNordic, fu_unifying_bootloader_nordic, FU, UNIFYING_BOOTLOADER_NORDIC, FuUnifyingBootloader)

G_END_DECLS

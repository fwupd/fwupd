/*
 * Copyright 2023 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_WCH_CH347_CFI_DEVICE (fu_wch_ch347_cfi_device_get_type())
G_DECLARE_FINAL_TYPE(FuWchCh347CfiDevice,
		     fu_wch_ch347_cfi_device,
		     FU,
		     WCH_CH347_CFI_DEVICE,
		     FuCfiDevice)

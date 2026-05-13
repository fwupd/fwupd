/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_WCH_CH341A_CFI_DEVICE (fu_wch_ch341a_cfi_device_get_type())
G_DECLARE_FINAL_TYPE(FuWchCh341aCfiDevice,
		     fu_wch_ch341a_cfi_device,
		     FU,
		     WCH_CH341A_CFI_DEVICE,
		     FuCfiDevice)

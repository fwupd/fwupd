/*
 * Copyright (C) 2020 Jimmy Yu <Jimmy_yu@pixart.com>
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"
#include "fu-pxi-common.h"

#define FU_TYPE_PXI_WIRELESS_MODULE (fu_pxi_wireless_module_get_type ())

G_DECLARE_FINAL_TYPE (FuPxiWirelessModule, fu_pxi_wireless_module, FU, PXI_WIRELESS_MODULE, FuDevice)

FuPxiWirelessModule *fu_pxi_wireless_module_new	(struct ota_fw_dev_model *model);

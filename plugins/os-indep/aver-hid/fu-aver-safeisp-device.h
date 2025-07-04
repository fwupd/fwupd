/*
 * Copyright 2024 Pierce Wang <pierce.wang@aver.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_AVER_SAFEISP_DEVICE (fu_aver_safeisp_device_get_type())
G_DECLARE_FINAL_TYPE(FuAverSafeispDevice,
		     fu_aver_safeisp_device,
		     FU,
		     AVER_SAFEISP_DEVICE,
		     FuHidDevice)

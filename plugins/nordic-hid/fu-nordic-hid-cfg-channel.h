/*
 * Copyright (C) 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_NORDIC_HID_CFG_CHANNEL (fu_nordic_hid_cfg_channel_get_type())
G_DECLARE_FINAL_TYPE(FuNordicHidCfgChannel,
		     fu_nordic_hid_cfg_channel,
		     FU,
		     NORDIC_HID_CFG_CHANNEL,
		     FuUdevDevice)

/*
 * Copyright (C) 2021 Ricardo Cañuelo <ricardo.canuelo@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_LOGITECH_HIDPP_RADIO (fu_logitech_hidpp_radio_get_type())
G_DECLARE_FINAL_TYPE(FuLogitechHidppRadio, fu_logitech_hidpp_radio, FU, HIDPP_RADIO, FuDevice)

FuLogitechHidppRadio *
fu_logitech_hidpp_radio_new(FuContext *ctx, guint8 entity);

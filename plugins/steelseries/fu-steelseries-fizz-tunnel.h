/*
 * Copyright (C) 2022 Gaël PORTAY <gael.portay@collabora.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-steelseries-fizz.h"

#define FU_TYPE_STEELSERIES_FIZZ_TUNNEL (fu_steelseries_fizz_tunnel_get_type())
G_DECLARE_FINAL_TYPE(FuSteelseriesFizzTunnel,
		     fu_steelseries_fizz_tunnel,
		     FU,
		     STEELSERIES_FIZZ_TUNNEL,
		     FuDevice)

FuSteelseriesFizzTunnel *
fu_steelseries_fizz_tunnel_new(FuSteelseriesFizz *parent);

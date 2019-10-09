/*
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"
#include "fu-synaprom-device.h"

#define FU_TYPE_SYNAPROM_CONFIG (fu_synaprom_config_get_type ())
G_DECLARE_FINAL_TYPE (FuSynapromConfig, fu_synaprom_config, FU, SYNAPROM_CONFIG, FuDevice)

FuSynapromConfig	*fu_synaprom_config_new		(FuSynapromDevice	*device);

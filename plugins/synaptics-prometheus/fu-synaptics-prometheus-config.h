/*
 * Copyright 2019 Richard Hughes <richard@hughsie.com>
 * Copyright 2019 Synaptics Inc
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-synaptics-prometheus-device.h"

#define FU_TYPE_SYNAPTICS_PROMETHEUS_CONFIG (fu_synaptics_prometheus_config_get_type())
G_DECLARE_FINAL_TYPE(FuSynapticsPrometheusConfig,
		     fu_synaptics_prometheus_config,
		     FU,
		     SYNAPTICS_PROMETHEUS_CONFIG,
		     FuDevice)

FuSynapticsPrometheusConfig *
fu_synaptics_prometheus_config_new(FuSynapticsPrometheusDevice *device);

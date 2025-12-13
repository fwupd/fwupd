/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

G_DECLARE_FINAL_TYPE(FuSynapticsPrometheusPlugin,
		     fu_synaptics_prometheus_plugin,
		     FU,
		     SYNAPTICS_PROMETHEUS_PLUGIN,
		     FuPlugin)

/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

G_DECLARE_FINAL_TYPE(FuSynapticsMstPlugin,
		     fu_synaptics_mst_plugin,
		     FU,
		     SYNAPTICS_MST_PLUGIN,
		     FuPlugin)

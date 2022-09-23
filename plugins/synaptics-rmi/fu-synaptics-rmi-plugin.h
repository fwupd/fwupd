/*
 * Copyright (C) 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

G_DECLARE_FINAL_TYPE(FuSynapticsRmiPlugin,
		     fu_synaptics_rmi_plugin,
		     FU,
		     SYNAPTICS_RMI_PLUGIN,
		     FuPlugin)

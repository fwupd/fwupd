/*
 * Copyright 2022 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

G_DECLARE_FINAL_TYPE(FuSynapticsCxaudioPlugin,
		     fu_synaptics_cxaudio_plugin,
		     FU,
		     SYNAPTICS_CXAUDIO_PLUGIN,
		     FuPlugin)

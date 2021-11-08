/*
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include "fu-plugin.h"

/**
 * fu_plugin_init_vfuncs:
 * @vfuncs: #FuPluginVfuncs
 *
 * Initializes the plugin vfuncs.
 *
 * Since: 1.7.2
 **/
void
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs);

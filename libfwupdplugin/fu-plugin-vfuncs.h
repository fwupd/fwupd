/*
 * Copyright 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
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
fu_plugin_init_vfuncs(FuPluginVfuncs *vfuncs) G_GNUC_NON_NULL(1);

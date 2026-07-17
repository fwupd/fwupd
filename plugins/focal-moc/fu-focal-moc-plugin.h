/*
 * Copyright 2024 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_FOCAL_MOC_PLUGIN (fu_focal_moc_plugin_get_type())
G_DECLARE_FINAL_TYPE(FuFocalMocPlugin,
		     fu_focal_moc_plugin,
		     FU,
		     FOCAL_MOC_PLUGIN,
		     FuPlugin)

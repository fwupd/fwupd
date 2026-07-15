/*
 * Copyright 2024 FocalTech Systems Co., Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_FOCALTECH_MOC_PLUGIN (fu_focaltech_moc_plugin_get_type())
G_DECLARE_FINAL_TYPE(FuFocaltechMocPlugin,
		     fu_focaltech_moc_plugin,
		     FU,
		     FOCALTECH_MOC_PLUGIN,
		     FuPlugin)

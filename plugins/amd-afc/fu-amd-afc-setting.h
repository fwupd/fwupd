/*
 * Copyright 2026 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-amd-afc-struct.h"

typedef struct {
	FuAmdAfcSettingKind kind;
	gchar *display_name;
	gchar *language;
	GPtrArray *paths;   /* gchar * */
	GPtrArray *options; /* FuAmdAfcOption * */
	guint16 question_id;
	guint8 question_flags;
	gboolean has_current;
	guint64 current;
	guint64 minimum;
	guint64 maximum;
	guint64 step;
	gboolean has_default;
	guint64 default_value;
} FuAmdAfcSetting;

void
fu_amd_afc_setting_export(FuAmdAfcSetting *self, FuFirmwareExportFlags flags, XbBuilderNode *bn);
gchar *
fu_amd_afc_setting_build_name(FuAmdAfcSetting *self);
const gchar *
fu_amd_afc_setting_get_option_name(FuAmdAfcSetting *self, guint64 value);
void
fu_amd_afc_setting_free(FuAmdAfcSetting *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(FuAmdAfcSetting, fu_amd_afc_setting_free)

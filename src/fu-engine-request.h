/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <fwupdplugin.h>

#include "fu-engine-struct.h"

#define FU_TYPE_ENGINE_REQUEST (fu_engine_request_get_type())
G_DECLARE_FINAL_TYPE(FuEngineRequest, fu_engine_request, FU, ENGINE_REQUEST, GObject)

FuEngineRequest *
fu_engine_request_new(const gchar *sender);
const gchar *
fu_engine_request_get_sender(FuEngineRequest *self) G_GNUC_NON_NULL(1);
void
fu_engine_request_add_flag(FuEngineRequest *self, FuEngineRequestFlag flag) G_GNUC_NON_NULL(1);
gboolean
fu_engine_request_has_flag(FuEngineRequest *self,
			   FuEngineRequestFlag flag) G_GNUC_WARN_UNUSED_RESULT G_GNUC_NON_NULL(1);
FwupdFeatureFlags
fu_engine_request_get_feature_flags(FuEngineRequest *self) G_GNUC_NON_NULL(1);
void
fu_engine_request_set_feature_flags(FuEngineRequest *self, FwupdFeatureFlags feature_flags)
    G_GNUC_NON_NULL(1);
const gchar *
fu_engine_request_get_locale(FuEngineRequest *self) G_GNUC_NON_NULL(1);
void
fu_engine_request_set_locale(FuEngineRequest *self, const gchar *locale) G_GNUC_NON_NULL(1);
gboolean
fu_engine_request_has_feature_flag(FuEngineRequest *self, FwupdFeatureFlags feature_flag)
    G_GNUC_NON_NULL(1);
gboolean
fu_engine_request_has_converter_flag(FuEngineRequest *self, FwupdCodecFlags device_flag)
    G_GNUC_NON_NULL(1);
FwupdCodecFlags
fu_engine_request_get_converter_flags(FuEngineRequest *self) G_GNUC_NON_NULL(1);
void
fu_engine_request_set_converter_flags(FuEngineRequest *self, FwupdCodecFlags device_flags)
    G_GNUC_NON_NULL(1);

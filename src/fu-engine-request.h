/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_ENGINE_REQUEST (fu_engine_request_get_type())
G_DECLARE_FINAL_TYPE(FuEngineRequest, fu_engine_request, FU, ENGINE_REQUEST, GObject)

typedef enum {
	FU_ENGINE_REQUEST_FLAG_NONE = 0,
	FU_ENGINE_REQUEST_FLAG_NO_REQUIREMENTS = 1 << 0,
	FU_ENGINE_REQUEST_FLAG_ANY_RELEASE = 1 << 1,
} FuEngineRequestFlags;

FuEngineRequest *
fu_engine_request_new(void);
void
fu_engine_request_add_string(FuEngineRequest *self, guint idt, GString *str);
void
fu_engine_request_add_flag(FuEngineRequest *self, FuEngineRequestFlags flag);
gboolean
fu_engine_request_has_flag(FuEngineRequest *self, FuEngineRequestFlags flag);
FwupdFeatureFlags
fu_engine_request_get_feature_flags(FuEngineRequest *self);
void
fu_engine_request_set_feature_flags(FuEngineRequest *self, FwupdFeatureFlags feature_flags);
const gchar *
fu_engine_request_get_locale(FuEngineRequest *self);
void
fu_engine_request_set_locale(FuEngineRequest *self, const gchar *locale);
gboolean
fu_engine_request_has_feature_flag(FuEngineRequest *self, FwupdFeatureFlags feature_flag);
gboolean
fu_engine_request_has_device_flag(FuEngineRequest *self, FwupdDeviceFlags device_flag);
FwupdDeviceFlags
fu_engine_request_get_device_flags(FuEngineRequest *self);
void
fu_engine_request_set_device_flags(FuEngineRequest *self, FwupdDeviceFlags device_flags);

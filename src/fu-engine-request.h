/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupd.h>

#define FU_TYPE_ENGINE_REQUEST (fu_engine_request_get_type ())
G_DECLARE_FINAL_TYPE (FuEngineRequest, fu_engine_request, FU, ENGINE_REQUEST, GObject)

FuEngineRequest		*fu_engine_request_new			(void);
FwupdFeatureFlags	 fu_engine_request_get_feature_flags	(FuEngineRequest	*self);
void			 fu_engine_request_set_feature_flags	(FuEngineRequest	*self,
								 FwupdFeatureFlags	 feature_flags);
FwupdDeviceFlags	 fu_engine_request_get_device_flags	(FuEngineRequest	*self);
void			 fu_engine_request_set_device_flags	(FuEngineRequest	*self,
								 FwupdDeviceFlags	 device_flags);

/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN				"FuEngine"

#include "config.h"

#include "fu-engine-request.h"

struct _FuEngineRequest
{
	GObject			 parent_instance;
	FwupdFeatureFlags	 feature_flags;
	FwupdDeviceFlags	 device_flags;
};

G_DEFINE_TYPE (FuEngineRequest, fu_engine_request, G_TYPE_OBJECT)

FwupdFeatureFlags
fu_engine_request_get_feature_flags (FuEngineRequest *self)
{
	g_return_val_if_fail (FU_IS_ENGINE_REQUEST (self), FALSE);
	return self->feature_flags;
}

void
fu_engine_request_set_feature_flags (FuEngineRequest *self,
				     FwupdFeatureFlags feature_flags)
{
	g_return_if_fail (FU_IS_ENGINE_REQUEST (self));
	self->feature_flags = feature_flags;
}

FwupdDeviceFlags
fu_engine_request_get_device_flags (FuEngineRequest *self)
{
	g_return_val_if_fail (FU_IS_ENGINE_REQUEST (self), FALSE);
	return self->device_flags;
}

void
fu_engine_request_set_device_flags (FuEngineRequest *self,
				    FwupdDeviceFlags device_flags)
{
	g_return_if_fail (FU_IS_ENGINE_REQUEST (self));
	self->device_flags = device_flags;
}

static void
fu_engine_request_init (FuEngineRequest *self)
{
	self->device_flags = FWUPD_DEVICE_FLAG_NONE;
	self->feature_flags = FWUPD_FEATURE_FLAG_NONE;
}

static void
fu_engine_request_class_init (FuEngineRequestClass *klass)
{
}

FuEngineRequest *
fu_engine_request_new (void)
{
	FuEngineRequest *self;
	self = g_object_new (FU_TYPE_ENGINE_REQUEST, NULL);
	return FU_ENGINE_REQUEST (self);
}

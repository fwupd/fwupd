/*
 * Copyright (C) 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuEngine"

#include "config.h"

#include "fu-engine-request.h"

struct _FuEngineRequest {
	GObject parent_instance;
	FuEngineRequestKind kind;
	FwupdFeatureFlags feature_flags;
	FwupdDeviceFlags device_flags;
	gchar *locale;
};

G_DEFINE_TYPE(FuEngineRequest, fu_engine_request, G_TYPE_OBJECT)

FwupdFeatureFlags
fu_engine_request_get_feature_flags(FuEngineRequest *self)
{
	g_return_val_if_fail(FU_IS_ENGINE_REQUEST(self), FALSE);
	return self->feature_flags;
}

const gchar *
fu_engine_request_get_locale(FuEngineRequest *self)
{
	g_return_val_if_fail(FU_IS_ENGINE_REQUEST(self), NULL);
	return self->locale;
}

FuEngineRequestKind
fu_engine_request_get_kind(FuEngineRequest *self)
{
	g_return_val_if_fail(FU_IS_ENGINE_REQUEST(self), FU_ENGINE_REQUEST_KIND_UNKNOWN);
	return self->kind;
}

void
fu_engine_request_set_feature_flags(FuEngineRequest *self, FwupdFeatureFlags feature_flags)
{
	g_return_if_fail(FU_IS_ENGINE_REQUEST(self));
	self->feature_flags = feature_flags;
}

void
fu_engine_request_set_locale(FuEngineRequest *self, const gchar *locale)
{
	g_return_if_fail(FU_IS_ENGINE_REQUEST(self));

	/* not changed */
	if (g_strcmp0(self->locale, locale) == 0)
		return;

	g_free(self->locale);
	self->locale = g_strdup(locale);

	/* remove the UTF8 suffix as it is not present in the XML */
	if (self->locale != NULL)
		g_strdelimit(self->locale, ".", '\0');
}

gboolean
fu_engine_request_has_feature_flag(FuEngineRequest *self, FwupdFeatureFlags feature_flag)
{
	g_return_val_if_fail(FU_IS_ENGINE_REQUEST(self), FALSE);
	return (self->feature_flags & feature_flag) > 0;
}

FwupdDeviceFlags
fu_engine_request_get_device_flags(FuEngineRequest *self)
{
	g_return_val_if_fail(FU_IS_ENGINE_REQUEST(self), FALSE);
	return self->device_flags;
}

void
fu_engine_request_set_device_flags(FuEngineRequest *self, FwupdDeviceFlags device_flags)
{
	g_return_if_fail(FU_IS_ENGINE_REQUEST(self));
	self->device_flags = device_flags;
}

gboolean
fu_engine_request_has_device_flag(FuEngineRequest *self, FwupdDeviceFlags device_flag)
{
	g_return_val_if_fail(FU_IS_ENGINE_REQUEST(self), FALSE);
	return (self->device_flags & device_flag) > 0;
}

static void
fu_engine_request_init(FuEngineRequest *self)
{
	self->device_flags = FWUPD_DEVICE_FLAG_NONE;
	self->feature_flags = FWUPD_FEATURE_FLAG_NONE;
}

static void
fu_engine_request_class_init(FuEngineRequestClass *klass)
{
}

FuEngineRequest *
fu_engine_request_new(FuEngineRequestKind kind)
{
	FuEngineRequest *self;
	self = g_object_new(FU_TYPE_ENGINE_REQUEST, NULL);
	self->kind = kind;
	return FU_ENGINE_REQUEST(self);
}

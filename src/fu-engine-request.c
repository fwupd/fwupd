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
	FuEngineRequestFlags flags;
	FwupdFeatureFlags feature_flags;
	FwupdDeviceFlags device_flags;
	gchar *locale;
};

G_DEFINE_TYPE(FuEngineRequest, fu_engine_request, G_TYPE_OBJECT)

static const gchar *
fu_engine_request_flag_to_string(FuEngineRequestFlags flag)
{
	if (flag == FU_ENGINE_REQUEST_FLAG_NO_REQUIREMENTS)
		return "no-requirements";
	if (flag == FU_ENGINE_REQUEST_FLAG_ANY_RELEASE)
		return "any-release";
	return NULL;
}

static gchar *
fu_engine_request_flags_to_string(FuEngineRequestFlags flags)
{
	g_autoptr(GString) str = g_string_new(NULL);
	for (guint i = 0; i < 64; i++) {
		if ((flags & ((guint64)1 << i)) == 0)
			continue;
		if (str->len > 0)
			g_string_append(str, "|");
		g_string_append(str, fu_engine_request_flag_to_string((guint64)1 << i));
	}
	return g_string_free(g_steal_pointer(&str), FALSE);
}

void
fu_engine_request_add_string(FuEngineRequest *self, guint idt, GString *str)
{
	g_return_if_fail(FU_IS_ENGINE_REQUEST(self));
	if (self->flags != FU_ENGINE_REQUEST_FLAG_NONE) {
		g_autofree gchar *flags = fu_engine_request_flags_to_string(self->flags);
		fu_string_append(str, idt, "Flags", flags);
	}
	fu_string_append_kx(str, idt, "FeatureFlags", self->feature_flags);
	fu_string_append_kx(str, idt, "DeviceFlags", self->device_flags);
	if (self->locale != NULL)
		fu_string_append(str, idt, "Locale", self->locale);
}

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

void
fu_engine_request_add_flag(FuEngineRequest *self, FuEngineRequestFlags flag)
{
	g_return_if_fail(FU_IS_ENGINE_REQUEST(self));
	self->flags |= flag;
}

gboolean
fu_engine_request_has_flag(FuEngineRequest *self, FuEngineRequestFlags flag)
{
	g_return_val_if_fail(FU_IS_ENGINE_REQUEST(self), FU_ENGINE_REQUEST_FLAG_NONE);
	return (self->flags & flag) > 0;
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
	self->flags = FU_ENGINE_REQUEST_FLAG_NONE;
	self->device_flags = FWUPD_DEVICE_FLAG_NONE;
	self->feature_flags = FWUPD_FEATURE_FLAG_NONE;
}

static void
fu_engine_request_finalize(GObject *obj)
{
	FuEngineRequest *self = FU_ENGINE_REQUEST(obj);
	g_free(self->locale);
	G_OBJECT_CLASS(fu_engine_request_parent_class)->finalize(obj);
}

static void
fu_engine_request_class_init(FuEngineRequestClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	object_class->finalize = fu_engine_request_finalize;
}

FuEngineRequest *
fu_engine_request_new(void)
{
	FuEngineRequest *self;
	self = g_object_new(FU_TYPE_ENGINE_REQUEST, NULL);
	return FU_ENGINE_REQUEST(self);
}

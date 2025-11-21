/*
 * Copyright 2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define G_LOG_DOMAIN "FuEngine"

#include "config.h"

#include <fwupd.h>

#include "fu-engine-request.h"

struct _FuEngineRequest {
	GObject parent_instance;
	FuEngineRequestFlags flags;
	FwupdFeatureFlags feature_flags;
	FwupdCodecFlags converter_flags;
	gchar *sender;
	gchar *locale;
};

static void
fu_engine_request_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_WITH_CODE(FuEngineRequest,
			fu_engine_request,
			G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC, fu_engine_request_codec_iface_init))

static void
fu_engine_request_add_string(FwupdCodec *codec, guint idt, GString *str)
{
	FuEngineRequest *self = FU_ENGINE_REQUEST(codec);
	if (self->flags != FU_ENGINE_REQUEST_FLAG_NONE) {
		g_autofree gchar *flags = fu_engine_request_flags_to_string(self->flags);
		fwupd_codec_string_append(str, idt, "Flags", flags);
	}
	fwupd_codec_string_append_hex(str, idt, "FeatureFlags", self->feature_flags);
	fwupd_codec_string_append_hex(str, idt, "ConverterFlags", self->converter_flags);
	fwupd_codec_string_append(str, idt, "Locale", self->locale);
}

static void
fu_engine_request_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_string = fu_engine_request_add_string;
}

const gchar *
fu_engine_request_get_sender(FuEngineRequest *self)
{
	g_return_val_if_fail(FU_IS_ENGINE_REQUEST(self), NULL);
	return self->sender;
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

FwupdCodecFlags
fu_engine_request_get_converter_flags(FuEngineRequest *self)
{
	g_return_val_if_fail(FU_IS_ENGINE_REQUEST(self), FALSE);
	return self->converter_flags;
}

void
fu_engine_request_set_converter_flags(FuEngineRequest *self, FwupdCodecFlags converter_flags)
{
	g_return_if_fail(FU_IS_ENGINE_REQUEST(self));
	self->converter_flags = converter_flags;
}

gboolean
fu_engine_request_has_converter_flag(FuEngineRequest *self, FwupdCodecFlags device_flag)
{
	g_return_val_if_fail(FU_IS_ENGINE_REQUEST(self), FALSE);
	return (self->converter_flags & device_flag) > 0;
}

static void
fu_engine_request_init(FuEngineRequest *self)
{
	self->flags = FU_ENGINE_REQUEST_FLAG_NONE;
	self->converter_flags = FWUPD_CODEC_FLAG_NONE;
	self->feature_flags = FWUPD_FEATURE_FLAG_NONE;
}

static void
fu_engine_request_finalize(GObject *obj)
{
	FuEngineRequest *self = FU_ENGINE_REQUEST(obj);
	g_free(self->sender);
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
fu_engine_request_new(const gchar *sender)
{
	FuEngineRequest *self;
	self = g_object_new(FU_TYPE_ENGINE_REQUEST, NULL);
	self->sender = g_strdup(sender);
	return FU_ENGINE_REQUEST(self);
}

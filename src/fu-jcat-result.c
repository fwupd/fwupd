/*
 * Copyright (C) 2017-2020 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include "fu-jcat-engine.h"
#include "fu-jcat-result.h"

struct _FuJcatResult {
	GObject parent_instance;
	gint64 timestamp;
	gchar *authority;
	FuJcatEngine *engine;
};

static void
fu_jcat_result_codec_iface_init(FwupdCodecInterface *iface);

G_DEFINE_TYPE_EXTENDED(FuJcatResult,
		       fu_jcat_result,
		       G_TYPE_OBJECT,
		       0,
		       G_IMPLEMENT_INTERFACE(FWUPD_TYPE_CODEC, fu_jcat_result_codec_iface_init));

enum { PROP_0, PROP_ENGINE, PROP_TIMESTAMP, PROP_AUTHORITY, PROP_LAST };

/**
 * fu_jcat_result_get_timestamp:
 * @self: #FuJcatResult
 *
 * Gets the signing timestamp, if set.
 *
 * Returns: UNIX timestamp, or 0 if unset
 **/
gint64
fu_jcat_result_get_timestamp(FuJcatResult *self)
{
	g_return_val_if_fail(FU_IS_JCAT_RESULT(self), 0);
	return self->timestamp;
}

/**
 * fu_jcat_result_get_authority:
 * @self: #FuJcatResult
 *
 * Gets the signing authority, if set.
 *
 * Returns: string, or %NULL
 **/
const gchar *
fu_jcat_result_get_authority(FuJcatResult *self)
{
	g_return_val_if_fail(FU_IS_JCAT_RESULT(self), NULL);
	return self->authority;
}

/**
 * fu_jcat_result_get_kind:
 * @self: #FuJcatResult
 *
 * Gets the blob kind.
 *
 * Returns: #FwupdJcatBlobKind, e.g. %FWUPD_JCAT_BLOB_KIND_SHA256
 **/
FwupdJcatBlobKind
fu_jcat_result_get_kind(FuJcatResult *self)
{
	g_return_val_if_fail(FU_IS_JCAT_RESULT(self), FWUPD_JCAT_BLOB_KIND_UNKNOWN);
	if (self->engine == NULL)
		return FWUPD_JCAT_BLOB_KIND_UNKNOWN;
	return fu_jcat_engine_get_kind(self->engine);
}

/**
 * fu_jcat_result_get_method:
 * @self: #FuJcatResult
 *
 * Gets the verification kind.
 *
 * Returns: #FwupdJcatBlobMethod, e.g. %FWUPD_JCAT_BLOB_METHOD_SIGNATURE
 **/
FwupdJcatBlobMethod
fu_jcat_result_get_method(FuJcatResult *self)
{
	g_return_val_if_fail(FU_IS_JCAT_RESULT(self), FWUPD_JCAT_BLOB_METHOD_UNKNOWN);
	if (self->engine == NULL)
		return FWUPD_JCAT_BLOB_METHOD_UNKNOWN;
	return fu_jcat_engine_get_method(self->engine);
}

static void
fu_jcat_result_add_string(FwupdCodec *codec, guint idt, GString *str)
{
	FuJcatResult *self = FU_JCAT_RESULT(codec);
	fwupd_codec_string_append(str, idt, G_OBJECT_TYPE_NAME(self), NULL);
	if (self->timestamp != 0) {
		g_autoptr(GDateTime) dt = g_date_time_new_from_unix_utc(self->timestamp);
		if (dt != NULL) {
#if GLIB_CHECK_VERSION(2, 62, 0)
			g_autofree gchar *tmp = g_date_time_format_iso8601(dt);
#else
			g_autofree gchar *tmp = g_date_time_format(dt, "%FT%TZ");
#endif
			fwupd_codec_string_append(str, idt + 1, "Timestamp", tmp);
		}
	}
	if (self->authority != NULL && self->authority[0] != '\0')
		fwupd_codec_string_append(str, idt + 1, "Authority", self->authority);
	if (self->engine != NULL)
		fwupd_codec_add_string(FWUPD_CODEC(self->engine), idt + 1, str);
}

static void
fu_jcat_result_finalize(GObject *object)
{
	FuJcatResult *self = FU_JCAT_RESULT(object);
	if (self->engine != NULL)
		g_object_unref(self->engine);
	g_free(self->authority);
	G_OBJECT_CLASS(fu_jcat_result_parent_class)->finalize(object);
}

static void
fu_jcat_result_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FuJcatResult *self = FU_JCAT_RESULT(object);
	switch (prop_id) {
	case PROP_ENGINE:
		g_value_set_object(value, self->engine);
		break;
	case PROP_TIMESTAMP:
		g_value_set_int64(value, self->timestamp);
		break;
	case PROP_AUTHORITY:
		g_value_set_string(value, self->authority);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_jcat_result_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FuJcatResult *self = FU_JCAT_RESULT(object);
	switch (prop_id) {
	case PROP_ENGINE:
		g_set_object(&self->engine, g_value_get_object(value));
		break;
	case PROP_TIMESTAMP:
		self->timestamp = g_value_get_int64(value);
		break;
	case PROP_AUTHORITY:
		g_free(self->authority);
		self->authority = g_value_dup_string(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
}

static void
fu_jcat_result_codec_iface_init(FwupdCodecInterface *iface)
{
	iface->add_string = fu_jcat_result_add_string;
}

static void
fu_jcat_result_class_init(FuJcatResultClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GParamSpec *pspec;

	object_class->get_property = fu_jcat_result_get_property;
	object_class->set_property = fu_jcat_result_set_property;
	object_class->finalize = fu_jcat_result_finalize;

	pspec =
	    g_param_spec_object("engine",
				NULL,
				NULL,
				FU_TYPE_JCAT_ENGINE,
				G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_ENGINE, pspec);

	pspec = g_param_spec_int64("timestamp",
				   NULL,
				   NULL,
				   0,
				   G_MAXINT64,
				   0,
				   G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_TIMESTAMP, pspec);

	pspec = g_param_spec_string("authority",
				    NULL,
				    NULL,
				    NULL,
				    G_PARAM_READWRITE | G_PARAM_STATIC_NAME);
	g_object_class_install_property(object_class, PROP_AUTHORITY, pspec);
}

static void
fu_jcat_result_init(FuJcatResult *self)
{
}
